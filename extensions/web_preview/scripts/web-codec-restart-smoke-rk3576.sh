#!/bin/sh
set -eu

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
PUBLISHER_BIN="${PUBLISHER_BIN:-${REMOTE_ROOT}/bin/camera_publisher_example}"
CODEC_BIN="${CODEC_BIN:-${REMOTE_ROOT}/bin/camera_codec_server}"
GATEWAY_BIN="${GATEWAY_BIN:-${REMOTE_ROOT}/bin/web_preview_gateway}"
DEVICE="${DEVICE:-/dev/video45}"
STREAM_ID="${STREAM_ID:-0}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings/web_codec_restart_records}"
STATIC_ROOT="${STATIC_ROOT:-${REMOTE_ROOT}/web_preview/dist}"
PORT="${PORT:-8080}"
MAX_PREVIEW_FPS="${MAX_PREVIEW_FPS:-30}"
RECORD_CONTAINER="${RECORD_CONTAINER:-raw_h264}"

case "$RECORD_CONTAINER" in
    raw_h264|mp4) ;;
    *)
        echo "unsupported RECORD_CONTAINER: $RECORD_CONTAINER" >&2
        exit 2
        ;;
esac

PUBLISHER_LOG="${PUBLISHER_LOG:-${REMOTE_ROOT}/logs/publisher_web_codec_restart.log}"
CODEC_LOG="${CODEC_LOG:-${REMOTE_ROOT}/logs/codec_web_codec_restart.log}"
GATEWAY_LOG="${GATEWAY_LOG:-${REMOTE_ROOT}/logs/gateway_web_codec_restart.log}"
PUBLISHER_PID_FILE="${PUBLISHER_PID_FILE:-${REMOTE_ROOT}/run/publisher_web_codec_restart.pid}"
CODEC_PID_FILE="${CODEC_PID_FILE:-${REMOTE_ROOT}/run/codec_web_codec_restart.pid}"
GATEWAY_PID_FILE="${GATEWAY_PID_FILE:-${REMOTE_ROOT}/run/gateway_web_codec_restart.pid}"
CLIENT_PY="${CLIENT_PY:-/tmp/web_codec_restart_smoke.py}"

cleanup()
{
    for f in "$GATEWAY_PID_FILE" "$CODEC_PID_FILE" "$PUBLISHER_PID_FILE"; do
        if [ -f "$f" ]; then
            kill "$(cat "$f")" 2>/dev/null || true
        fi
    done
}

dump_logs()
{
    echo "GATEWAY_LOG"
    tail -100 "$GATEWAY_LOG" || true
    echo "CODEC_LOG"
    tail -120 "$CODEC_LOG" || true
    echo "PUBLISHER_LOG"
    tail -80 "$PUBLISHER_LOG" || true
    echo "RECORD_FILES"
    ls -lh "$OUTPUT_DIR" | head -30 || true
}

start_codec()
{
    rm -f "$CODEC_SOCKET"
    "$CODEC_BIN" \
        --control-socket "$CONTROL_SOCKET" \
        --data-socket "$DATA_SOCKET" \
        --codec-socket "$CODEC_SOCKET" \
        --output-dir "$OUTPUT_DIR" \
        --device "$DEVICE" \
        --stream-id "$STREAM_ID" \
        >> "$CODEC_LOG" 2>&1 &
    echo "$!" > "$CODEC_PID_FILE"
    sleep 1
}

trap cleanup EXIT INT TERM

rm -f "$CONTROL_SOCKET" "$DATA_SOCKET" "$CODEC_SOCKET"
rm -rf "$OUTPUT_DIR" "$PUBLISHER_LOG" "$CODEC_LOG" "$GATEWAY_LOG" \
       "$PUBLISHER_PID_FILE" "$CODEC_PID_FILE" "$GATEWAY_PID_FILE"
mkdir -p "$OUTPUT_DIR" "$STATIC_ROOT"
chmod +x "$PUBLISHER_BIN" "$CODEC_BIN" "$GATEWAY_BIN"

cat > "$CLIENT_PY" <<'PY'
import base64
import json
import os
import signal
import socket
import struct
import sys
import time

HOST = '127.0.0.1'
PORT = int(os.environ.get('PORT', '8080'))
RECORD_CONTAINER = os.environ.get('RECORD_CONTAINER', 'raw_h264')
STREAM_ID = os.environ.get('STREAM_ID', '0')
CODEC_PID_FILE = os.environ.get('CODEC_PID_FILE', '')
PHASE = os.environ.get('PHASE', 'failover')

def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise RuntimeError('socket closed')
        data += chunk
    return data

def recv_frame(sock, timeout=1.0):
    sock.settimeout(timeout)
    try:
        first = recv_exact(sock, 2)
    except socket.timeout:
        return None, b''
    sock.settimeout(5.0)
    opcode = first[0] & 0x0f
    length = first[1] & 0x7f
    if length == 126:
        length = struct.unpack('!H', recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack('!Q', recv_exact(sock, 8))[0]
    payload = recv_exact(sock, length) if length else b''
    return opcode, payload

def send_text(sock, text):
    payload = text.encode()
    mask = b'\x11\x22\x33\x44'
    header = bytearray([0x81])
    if len(payload) <= 125:
        header.append(0x80 | len(payload))
    elif len(payload) <= 0xffff:
        header.append(0x80 | 126)
        header.extend(struct.pack('!H', len(payload)))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack('!Q', len(payload)))
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(bytes(header) + mask + masked)

def connect_ws():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        'GET /ws HTTP/1.1\r\n'
        f'Host: {HOST}:{PORT}\r\n'
        'Upgrade: websocket\r\n'
        'Connection: Upgrade\r\n'
        f'Sec-WebSocket-Key: {key}\r\n'
        'Sec-WebSocket-Version: 13\r\n\r\n'
    )
    sock.sendall(request.encode())
    response = b''
    while b'\r\n\r\n' not in response:
        response += sock.recv(1)
    if b'101' not in response.split(b'\r\n', 1)[0]:
        raise RuntimeError(response.decode(errors='replace'))
    return sock

def count_binary_frames(sock, seconds):
    deadline = time.time() + seconds
    count = 0
    text = []
    while time.time() < deadline:
        opcode, payload = recv_frame(sock, timeout=0.5)
        if opcode is None:
            continue
        if opcode == 0x2:
            count += 1
        elif opcode == 0x1:
            text.append(payload.decode(errors='replace'))
        elif opcode == 0x8:
            raise RuntimeError('websocket closed')
    return count, text

def record_status_items(items):
    out = []
    for item in items:
        try:
            payload = json.loads(item)
        except json.JSONDecodeError:
            continue
        if payload.get('type') == 'record_status':
            out.append(payload)
    return out

def start_recording(sock):
    cmd = {
        'type': 'set_record_enabled',
        'stream_id': STREAM_ID,
        'enabled': True,
        'container': RECORD_CONTAINER,
    }
    send_text(sock, json.dumps(cmd, separators=(',', ':')))

def stop_recording(sock):
    send_text(sock, json.dumps({
        'type': 'set_record_enabled',
        'stream_id': STREAM_ID,
        'enabled': False,
    }, separators=(',', ':')))

def dump_status(prefix, statuses):
    for item in statuses:
        print(prefix + json.dumps(item, sort_keys=True, separators=(',', ':')), flush=True)

def kill_codec():
    with open(CODEC_PID_FILE, 'r', encoding='utf-8') as f:
        pid = int(f.read().strip())
    os.kill(pid, signal.SIGTERM)
    time.sleep(1.0)

def run_failover():
    sock = connect_ws()
    before, text_before = count_binary_frames(sock, 2.0)
    start_recording(sock)
    during, text_during = count_binary_frames(sock, 3.0)
    statuses = record_status_items(text_before + text_during)
    dump_status('WS_RECORD_STATUS ', statuses)
    if before <= 0 or during <= 0:
        raise SystemExit(2)
    if not any(item.get('recording') is True for item in statuses):
        raise SystemExit(3)

    kill_codec()
    after_kill, text_after_kill = count_binary_frames(sock, 2.0)
    stop_recording(sock)
    after_stop, text_after_stop = count_binary_frames(sock, 2.0)
    stop_statuses = record_status_items(text_after_kill + text_after_stop)
    dump_status('WS_ERROR_STATUS ', stop_statuses)
    print(
        f'CODEC_RESTART_FAILOVER before={before} during={during} '
        f'after_kill={after_kill} after_stop={after_stop}',
        flush=True,
    )
    if after_kill <= 0 or after_stop <= 0:
        raise SystemExit(4)
    if not any(item.get('recording') is False and item.get('error') for item in stop_statuses):
        raise SystemExit(5)
    sock.close()

def run_recover():
    sock = connect_ws()
    before, text_before = count_binary_frames(sock, 2.0)
    start_recording(sock)
    during, text_during = count_binary_frames(sock, 3.0)
    stop_recording(sock)
    after, text_after = count_binary_frames(sock, 3.0)
    statuses = record_status_items(text_before + text_during + text_after)
    dump_status('WS_RECOVER_STATUS ', statuses)
    print(f'CODEC_RESTART_RECOVER before={before} during={during} after={after}', flush=True)
    if before <= 0 or during <= 0 or after <= 0:
        raise SystemExit(6)
    if not any(item.get('recording') is True for item in statuses):
        raise SystemExit(7)
    if not any(item.get('recording') is False and not item.get('error') for item in statuses):
        raise SystemExit(8)
    sock.close()

if PHASE == 'failover':
    run_failover()
elif PHASE == 'recover':
    run_recover()
else:
    print(f'unsupported PHASE: {PHASE}', file=sys.stderr)
    raise SystemExit(2)
PY

"$PUBLISHER_BIN" "$DEVICE" "$CONTROL_SOCKET" "$DATA_SOCKET" --io-method mmap \
    > "$PUBLISHER_LOG" 2>&1 &
echo "$!" > "$PUBLISHER_PID_FILE"
sleep 1

start_codec

"$GATEWAY_BIN" \
    --control-socket "$CONTROL_SOCKET" \
    --data-socket "$DATA_SOCKET" \
    --codec-socket "$CODEC_SOCKET" \
    --device "$DEVICE" \
    --stream-id "$STREAM_ID" \
    --static-root "$STATIC_ROOT" \
    --output-dir "$OUTPUT_DIR" \
    --port "$PORT" \
    --max-fps "$MAX_PREVIEW_FPS" \
    > "$GATEWAY_LOG" 2>&1 &
echo "$!" > "$GATEWAY_PID_FILE"
sleep 2

set +e
PORT="$PORT" \
RECORD_CONTAINER="$RECORD_CONTAINER" \
CODEC_PID_FILE="$CODEC_PID_FILE" \
PHASE=failover \
python3 "$CLIENT_PY"
failover_rc=$?
set -e

if [ "$failover_rc" -ne 0 ]; then
    dump_logs
    exit "$failover_rc"
fi

start_codec

set +e
PORT="$PORT" \
RECORD_CONTAINER="$RECORD_CONTAINER" \
CODEC_PID_FILE="$CODEC_PID_FILE" \
PHASE=recover \
python3 "$CLIENT_PY"
recover_rc=$?
set -e

cleanup
sleep 1
trap - EXIT INT TERM

expected_ext="h264"
if [ "$RECORD_CONTAINER" = "mp4" ]; then
    expected_ext="mp4"
fi
record_count="$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name "*.${expected_ext}" -size +0c | wc -l | tr -d ' ')"
if [ "$recover_rc" -eq 0 ] && [ "$record_count" -lt 1 ]; then
    echo "record file count too small after recovery: ext=${expected_ext} count=${record_count}" >&2
    recover_rc=9
fi

dump_logs
exit "$recover_rc"
