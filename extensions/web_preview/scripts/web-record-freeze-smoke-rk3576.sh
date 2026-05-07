#!/bin/sh
set -eu

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
PUBLISHER_BIN="${PUBLISHER_BIN:-${REMOTE_ROOT}/bin/camera_publisher_example}"
CODEC_BIN="${CODEC_BIN:-${REMOTE_ROOT}/bin/camera_codec_server}"
GATEWAY_BIN="${GATEWAY_BIN:-${REMOTE_ROOT}/bin/web_preview_gateway}"
DEVICE="${DEVICE:-/dev/video45}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings/web_freeze_records}"
STATIC_ROOT="${STATIC_ROOT:-${REMOTE_ROOT}/web_preview/dist}"
PORT="${PORT:-8080}"
MAX_PREVIEW_FPS="${MAX_PREVIEW_FPS:-30}"
RECORD_CONTAINER="${RECORD_CONTAINER:-raw_h264}"
RECORD_CYCLES="${RECORD_CYCLES:-1}"
RECONNECT_AFTER_STOP="${RECONNECT_AFTER_STOP:-1}"
case "$RECORD_CONTAINER" in
    raw_h264|mp4) ;;
    *)
        echo "unsupported RECORD_CONTAINER: $RECORD_CONTAINER" >&2
        exit 2
        ;;
esac
case "$RECORD_CYCLES" in
    ''|*[!0-9]*)
        echo "invalid RECORD_CYCLES: $RECORD_CYCLES" >&2
        exit 2
        ;;
esac
if [ "$RECORD_CYCLES" -lt 1 ]; then
    echo "RECORD_CYCLES must be >= 1" >&2
    exit 2
fi

PUBLISHER_LOG="${PUBLISHER_LOG:-${REMOTE_ROOT}/logs/publisher_web_freeze.log}"
CODEC_LOG="${CODEC_LOG:-${REMOTE_ROOT}/logs/codec_web_freeze.log}"
GATEWAY_LOG="${GATEWAY_LOG:-${REMOTE_ROOT}/logs/gateway_web_freeze.log}"
PUBLISHER_PID_FILE="${PUBLISHER_PID_FILE:-${REMOTE_ROOT}/run/publisher_web_freeze.pid}"
CODEC_PID_FILE="${CODEC_PID_FILE:-${REMOTE_ROOT}/run/codec_web_freeze.pid}"
GATEWAY_PID_FILE="${GATEWAY_PID_FILE:-${REMOTE_ROOT}/run/gateway_web_freeze.pid}"
CLIENT_PY="${CLIENT_PY:-/tmp/web_record_freeze_smoke.py}"

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
    tail -80 "$GATEWAY_LOG" || true
    echo "CODEC_LOG"
    tail -80 "$CODEC_LOG" || true
    echo "PUBLISHER_LOG"
    tail -80 "$PUBLISHER_LOG" || true
    echo "RECORD_FILES"
    ls -lh "$OUTPUT_DIR" | head -20 || true
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
import socket
import struct
import time

HOST = '127.0.0.1'
PORT = int(os.environ.get('PORT', '8080'))
RECORD_CONTAINER = os.environ.get('RECORD_CONTAINER', 'raw_h264')
RECORD_CYCLES = int(os.environ.get('RECORD_CYCLES', '1'))
RECONNECT_AFTER_STOP = os.environ.get('RECONNECT_AFTER_STOP', '1') not in ('0', 'false', 'False')

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

sock = connect_ws()
all_status = []
for cycle in range(1, RECORD_CYCLES + 1):
    before, text_before = count_binary_frames(sock, 2.0)
    start_cmd = {
        'type': 'set_record_enabled',
        'stream_id': '0',
        'enabled': True,
        'container': RECORD_CONTAINER,
    }
    send_text(sock, json.dumps(start_cmd, separators=(',', ':')))
    during, text_during = count_binary_frames(sock, 3.0)
    send_text(sock, '{"type":"set_record_enabled","stream_id":"0","enabled":false}')
    after, text_after = count_binary_frames(sock, 3.0)

    statuses = record_status_items(text_before + text_during + text_after)
    all_status.extend(statuses)
    print(f'WS_COUNTS cycle={cycle} before={before} during={during} after={after}', flush=True)
    for item in statuses:
        print('WS_RECORD_STATUS ' + json.dumps(item, sort_keys=True, separators=(',', ':')), flush=True)

    if before <= 0 or during <= 0 or after <= 0:
        raise SystemExit(2)
    if not any(item.get('recording') is True for item in statuses):
        raise SystemExit(3)
    if not any(item.get('recording') is False and not item.get('error') for item in statuses):
        raise SystemExit(4)

    if RECONNECT_AFTER_STOP:
        sock.close()
        sock = connect_ws()
        reconnect, reconnect_text = count_binary_frames(sock, 2.0)
        print(f'WS_RECONNECT cycle={cycle} frames={reconnect}', flush=True)
        all_status.extend(record_status_items(reconnect_text))
        if reconnect <= 0:
            raise SystemExit(5)

if not all(item.get('container', RECORD_CONTAINER) == RECORD_CONTAINER for item in all_status if item.get('recording') is True):
    raise SystemExit(6)
sock.close()
PY

"$PUBLISHER_BIN" "$DEVICE" "$CONTROL_SOCKET" "$DATA_SOCKET" --io-method mmap \
    > "$PUBLISHER_LOG" 2>&1 &
echo "$!" > "$PUBLISHER_PID_FILE"
sleep 1

"$CODEC_BIN" \
    --control-socket "$CONTROL_SOCKET" \
    --data-socket "$DATA_SOCKET" \
    --codec-socket "$CODEC_SOCKET" \
    --output-dir "$OUTPUT_DIR" \
    --device "$DEVICE" \
    > "$CODEC_LOG" 2>&1 &
echo "$!" > "$CODEC_PID_FILE"
sleep 1

"$GATEWAY_BIN" \
    --control-socket "$CONTROL_SOCKET" \
    --data-socket "$DATA_SOCKET" \
    --codec-socket "$CODEC_SOCKET" \
    --device "$DEVICE" \
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
RECORD_CYCLES="$RECORD_CYCLES" \
RECONNECT_AFTER_STOP="$RECONNECT_AFTER_STOP" \
python3 "$CLIENT_PY"
client_rc=$?
set -e

cleanup
sleep 1
trap - EXIT INT TERM

expected_ext="h264"
if [ "$RECORD_CONTAINER" = "mp4" ]; then
    expected_ext="mp4"
fi
record_count="$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name "*.${expected_ext}" | wc -l | tr -d ' ')"
if [ "$client_rc" -eq 0 ] && [ "$record_count" -lt "$RECORD_CYCLES" ]; then
    echo "record file count too small: ext=${expected_ext} count=${record_count} expected=${RECORD_CYCLES}" >&2
    client_rc=6
fi
if [ "$client_rc" -eq 0 ] && [ "$RECORD_CONTAINER" = "mp4" ] && command -v ffprobe >/dev/null 2>&1; then
    newest_mp4="$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name '*.mp4' | sort | tail -1)"
    if [ -n "$newest_mp4" ]; then
        ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height \
            -of default=noprint_wrappers=1 "$newest_mp4"
    fi
fi

dump_logs
exit "$client_rc"
