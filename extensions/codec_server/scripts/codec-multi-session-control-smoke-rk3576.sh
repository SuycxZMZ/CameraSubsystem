#!/bin/sh
set -eu

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
CODEC_BIN="${CODEC_BIN:-${REMOTE_ROOT}/bin/camera_codec_server}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec_multi.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings/codec_multi_session_control}"
CODEC_LOG="${CODEC_LOG:-${REMOTE_ROOT}/logs/codec_multi_session_control.log}"
CODEC_PID_FILE="${CODEC_PID_FILE:-${REMOTE_ROOT}/run/codec_multi_session_control.pid}"
CLIENT_PY="${CLIENT_PY:-/tmp/codec_multi_session_control_smoke.py}"

cleanup()
{
    if [ -f "$CODEC_PID_FILE" ]; then
        kill "$(cat "$CODEC_PID_FILE")" 2>/dev/null || true
        rm -f "$CODEC_PID_FILE"
    fi
    rm -f "$CODEC_SOCKET"
}

trap cleanup EXIT INT TERM

rm -f "$CODEC_SOCKET" "$CODEC_LOG" "$CODEC_PID_FILE"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR" "$(dirname "$CODEC_LOG")" "$(dirname "$CODEC_PID_FILE")"
chmod +x "$CODEC_BIN"

"$CODEC_BIN" \
    --codec-socket "$CODEC_SOCKET" \
    --output-dir "$OUTPUT_DIR" \
    --disable-camera-subscriber \
    > "$CODEC_LOG" 2>&1 &
echo "$!" > "$CODEC_PID_FILE"
sleep 1

cat > "$CLIENT_PY" <<'PY'
import json
import os
import socket
import sys
import time

SOCKET_PATH = os.environ.get('CODEC_SOCKET', '/tmp/camera_subsystem_codec_multi.sock')

def send(payload):
    line = json.dumps(payload, separators=(',', ':')).encode() + b'\n'
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(3.0)
        sock.connect(SOCKET_PATH)
        sock.sendall(line)
        data = b''
        while not data.endswith(b'\n'):
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    if not data:
        raise RuntimeError('empty codec response')
    return json.loads(data.decode().strip())

def expect(name, condition, response):
    if not condition:
        print(name + ' FAIL ' + json.dumps(response, sort_keys=True), flush=True)
        sys.exit(2)
    print(name + ' PASS ' + json.dumps(response, sort_keys=True, separators=(',', ':')), flush=True)

cam0 = 'control_cam_0'
cam1 = 'control_cam_1'

r0 = send({'type': 'start_recording', 'request_id': 'm0-start', 'stream_id': cam0, 'container': 'raw_h264'})
expect('cam0_start', r0.get('recording') is True and r0.get('state') == 'recording', r0)

r1 = send({'type': 'start_recording', 'request_id': 'm1-start', 'stream_id': cam1, 'container': 'raw_h264'})
expect('cam1_start', r1.get('recording') is True and r1.get('state') == 'recording', r1)
expect('files_are_isolated', r0.get('file') and r1.get('file') and r0.get('file') != r1.get('file'), {'cam0': r0.get('file'), 'cam1': r1.get('file')})

s0 = send({'type': 'status', 'request_id': 'm0-status', 'stream_id': cam0, 'container': 'raw_h264'})
expect('cam0_status_recording', s0.get('recording') is True and s0.get('state') == 'recording', s0)

stop0 = send({'type': 'stop_recording', 'request_id': 'm0-stop', 'stream_id': cam0, 'container': 'raw_h264'})
expect('cam0_stop', stop0.get('recording') is False and stop0.get('state') == 'idle', stop0)

s1 = send({'type': 'status', 'request_id': 'm1-status', 'stream_id': cam1, 'container': 'raw_h264'})
expect('cam1_survives_cam0_stop', s1.get('recording') is True and s1.get('state') == 'recording', s1)

dup0 = send({'type': 'stop_recording', 'request_id': 'm0-stop-again', 'stream_id': cam0, 'container': 'raw_h264'})
expect('cam0_second_stop_error', dup0.get('recording') is False and dup0.get('error') == 'not_recording', dup0)

stop1 = send({'type': 'stop_recording', 'request_id': 'm1-stop', 'stream_id': cam1, 'container': 'raw_h264'})
expect('cam1_stop', stop1.get('recording') is False and stop1.get('state') == 'idle', stop1)
PY

CODEC_SOCKET="$CODEC_SOCKET" python3 "$CLIENT_PY"

echo "CODEC_LOG"
tail -80 "$CODEC_LOG" || true
echo "RECORD_FILES"
ls -lh "$OUTPUT_DIR" | head -20 || true
echo "codec_multi_session_control_result=PASS"
