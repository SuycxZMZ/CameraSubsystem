#!/usr/bin/env bash
set -euo pipefail

# RK3576 Web Preview + Recording full-stack test
# Starts publisher, codec_server, and gateway, then verifies
# Web preview and recording via browser-accessible HTTP endpoints.

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"

DEVICE="${DEVICE:-/dev/video45}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings}"
HTTP_PORT="${HTTP_PORT:-8080}"

RECORD_SECONDS="${RECORD_SECONDS:-10}"

remote_bin="${REMOTE_ROOT}/bin"
remote_web="${REMOTE_ROOT}/web_preview/dist"
remote_logs="${REMOTE_ROOT}/logs"
remote_run="${REMOTE_ROOT}/run"

PUBLISHER_LOG="${remote_logs}/publisher_fullstack.log"
CODEC_LOG="${remote_logs}/codec_fullstack.log"
GATEWAY_LOG="${remote_logs}/gateway_fullstack.log"
PUBLISHER_PID="${remote_run}/publisher.pid"
CODEC_PID="${remote_run}/codec.pid"
GATEWAY_PID="${remote_run}/gateway.pid"

echo "============================================"
echo "CameraSubsystem Full-Stack Test on RK3576"
echo "============================================"
echo "Board:       ${BOARD_USER}@${BOARD_HOST}"
echo "Remote root: ${REMOTE_ROOT}"
echo "Device:      ${DEVICE}"
echo "HTTP port:   ${HTTP_PORT}"
echo "Record sec:  ${RECORD_SECONDS}"
echo ""

# --- Cleanup ---
cleanup()
{
    echo ""
    echo "=== Stopping services ==="
    ssh "${BOARD_USER}@${BOARD_HOST}" "
        if [ -f '${CODEC_PID}' ]; then kill \$(cat '${CODEC_PID}') 2>/dev/null || true; fi
        if [ -f '${GATEWAY_PID}' ]; then kill \$(cat '${GATEWAY_PID}') 2>/dev/null || true; fi
        if [ -f '${PUBLISHER_PID}' ]; then kill \$(cat '${PUBLISHER_PID}') 2>/dev/null || true; fi
        sleep 1
        rm -f '${CODEC_PID}' '${GATEWAY_PID}' '${PUBLISHER_PID}'
    " 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# --- Step 1: Start publisher ---
echo "[1/5] Starting camera_publisher_example..."
ssh "${BOARD_USER}@${BOARD_HOST}" "
    mkdir -p '${remote_logs}' '${remote_run}' '${OUTPUT_DIR}'
    rm -f '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${CODEC_SOCKET}'

    '${remote_bin}/camera_publisher_example' '${DEVICE}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' --io-method mmap \
        > '${PUBLISHER_LOG}' 2>&1 &
    echo \$! > '${PUBLISHER_PID}'
    sleep 1
    echo '  Publisher PID: '\$(cat '${PUBLISHER_PID}')
"
sleep 1

# --- Step 2: Start codec_server ---
echo "[2/5] Starting camera_codec_server..."
ssh "${BOARD_USER}@${BOARD_HOST}" "
    '${remote_bin}/camera_codec_server' \
        --control-socket '${CONTROL_SOCKET}' \
        --data-socket '${DATA_SOCKET}' \
        --codec-socket '${CODEC_SOCKET}' \
        --output-dir '${OUTPUT_DIR}' \
        --device '${DEVICE}' \
        > '${CODEC_LOG}' 2>&1 &
    echo \$! > '${CODEC_PID}'
    sleep 1
    echo '  Codec PID: '\$(cat '${CODEC_PID}')
"
sleep 1

# --- Step 3: Start web_preview_gateway ---
echo "[3/5] Starting web_preview_gateway..."
ssh "${BOARD_USER}@${BOARD_HOST}" "
    '${remote_bin}/web_preview_gateway' \
        --port ${HTTP_PORT} \
        --control-socket '${CONTROL_SOCKET}' \
        --data-socket '${DATA_SOCKET}' \
        --codec-socket '${CODEC_SOCKET}' \
        --output-dir '${OUTPUT_DIR}' \
        --device '${DEVICE}' \
        --static-root '${remote_web}' \
        > '${GATEWAY_LOG}' 2>&1 &
    echo \$! > '${GATEWAY_PID}'
    sleep 1
    echo '  Gateway PID: '\$(cat '${GATEWAY_PID}')
"
sleep 1

# --- Step 4: Verify Web preview ---
echo "[4/5] Verifying Web preview..."
sleep 2

# Check HTTP status endpoint
status=$(ssh "${BOARD_USER}@${BOARD_HOST}" "
    curl -s http://localhost:${HTTP_PORT}/status 2>/dev/null || echo 'HTTP_FAIL'
")
if echo "$status" | grep -q '"type":"status"'; then
    echo "  PASS: /status endpoint returns valid JSON"
else
    echo "  WARN: /status endpoint not responding (may need more startup time)"
fi

# Check WebSocket frame count via Python
ws_result=$(ssh "${BOARD_USER}@${BOARD_HOST}" "
    python3 -c \"
import socket, time, struct, json

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
try:
    sock.connect(('127.0.0.1', ${HTTP_PORT}))
except:
    print('WS_CONNECT_FAIL')
    exit()

# Minimal WebSocket handshake
import hashlib, base64
key = base64.b64encode(b'test12345678test').decode()
req = f'GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'
sock.sendall(req.encode())
resp = b''
while b'\r\n\r\n' not in resp:
    resp += sock.recv(4096)

# Read binary frames for 3 seconds
frames = 0
start = time.time()
while time.time() - start < 3:
    sock.settimeout(1)
    try:
        h = sock.recv(2)
        if len(h) < 2: break
        plen = h[1] & 0x7f
        if plen == 126:
            ext = sock.recv(2)
            plen = struct.unpack('>H', ext)[0]
        elif plen == 127:
            ext = sock.recv(8)
            plen = struct.unpack('>Q', ext)[0]
        if h[1] & 0x80:
            mask = sock.recv(4)
        if plen > 0:
            data = b''
            while len(data) < plen:
                data += sock.recv(plen - len(data))
        frames += 1
    except socket.timeout:
        continue
    except:
        break

sock.close()
print(f'WS_FRAMES={frames}')
\" 2>/dev/null || echo 'WS_TEST_FAIL'
")

if echo "$ws_result" | grep -q "WS_FRAMES="; then
    frame_count=$(echo "$ws_result" | grep -oP 'WS_FRAMES=\K[0-9]+')
    if [ "$frame_count" -gt 0 ]; then
        echo "  PASS: WebSocket received ${frame_count} frames in 3 seconds"
    else
        echo "  WARN: WebSocket received 0 frames"
    fi
else
    echo "  WARN: WebSocket test failed or unavailable"
fi

# --- Step 5: Test recording via codec control ---
echo "[5/5] Testing recording via codec control socket..."
record_result=$(ssh "${BOARD_USER}@${BOARD_HOST}" "
    python3 -c \"
import socket, json, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('${CODEC_SOCKET}')

def send(cmd):
    sock.sendall((cmd + '\n').encode())
    data = b''
    while not data.endswith(b'\n'):
        chunk = sock.recv(4096)
        if not chunk: break
        data += chunk
    return json.loads(data.decode().strip())

# Start recording
start = send('{\"type\":\"start_recording\",\"request_id\":\"ft1\",\"stream_id\":\"usb_camera_0\",\"output_dir\":\"${OUTPUT_DIR}\"}')
if not start.get('recording'):
    print('FAIL: start_recording did not return recording=true')
    exit(1)
print(f'START: recording={start.get(\"recording\")} state={start.get(\"state\")}')

# Record for specified duration
time.sleep(${RECORD_SECONDS})

# Check status
status = send('{\"type\":\"status\",\"request_id\":\"ft2\",\"stream_id\":\"usb_camera_0\"}')
print(f'STATUS: input={status.get(\"input_frames\",0)} decoded={status.get(\"decoded_frames\",0)} encoded={status.get(\"encoded_frames\",0)} decode_fail={status.get(\"decode_failures\",0)} write_fail={status.get(\"write_failures\",0)}')

# Stop recording
stop = send('{\"type\":\"stop_recording\",\"request_id\":\"ft3\",\"stream_id\":\"usb_camera_0\"}')
print(f'STOP: recording={stop.get(\"recording\")} state={stop.get(\"state\")} encoded={stop.get(\"encoded_frames\",0)}')

sock.close()

# Validate
failures = []
if status.get('input_frames', 0) <= 0:
    failures.append('no input frames')
if status.get('encoded_frames', 0) <= 0:
    failures.append('no encoded frames')
if status.get('decode_failures', 0) > 10:
    failures.append(f'too many decode failures: {status.get(\"decode_failures\")}')
if status.get('write_failures', 0) > 0:
    failures.append(f'write failures: {status.get(\"write_failures\")}')

if failures:
    print('RECORD_RESULT=FAIL')
    for f in failures:
        print(f'  - {f}')
else:
    print('RECORD_RESULT=PASS')
\" 2>/dev/null || echo 'RECORD_TEST_ERROR'
")

echo ""
echo "$record_result"

# --- Collect results ---
echo ""
echo "=== Service Logs (last 10 lines each) ==="
ssh "${BOARD_USER}@${BOARD_HOST}" "
    echo '--- Publisher ---'
    tail -10 '${PUBLISHER_LOG}' 2>/dev/null || true
    echo '--- Codec ---'
    tail -10 '${CODEC_LOG}' 2>/dev/null || true
    echo '--- Gateway ---'
    tail -10 '${GATEWAY_LOG}' 2>/dev/null || true
"

echo ""
echo "=== Recording Output Files ==="
ssh "${BOARD_USER}@${BOARD_HOST}" "
    ls -lh '${OUTPUT_DIR}'/*.h264 2>/dev/null | head -10 || echo 'No .h264 files found'
    echo ''
    total=0
    for f in '${OUTPUT_DIR}'/*.h264; do
        if [ -f \"\$f\" ]; then
            s=\$(stat -c%s \"\$f\")
            total=\$((total + s))
        fi
    done
    echo \"Total recording size: \$((total / 1024 / 1024)) MB\"
"

echo ""
echo "=== Full-Stack Test Summary ==="
echo "Web URL: http://${BOARD_HOST}:${HTTP_PORT}"
echo "Recording dir: ${OUTPUT_DIR}"
echo ""
echo "To verify in browser:"
echo "  1. Open http://${BOARD_HOST}:${HTTP_PORT} in Chrome/Firefox"
echo "  2. Click Play button to start preview"
echo "  3. Click Record button (circle icon) to start recording"
echo "  4. Wait a few seconds, click Record again to stop"
echo "  5. Check ${OUTPUT_DIR}/*.h264 on the board"
echo "  6. Download .h264 file and play with ffplay/VLC"
echo ""

# Final result
if echo "$record_result" | grep -q "RECORD_RESULT=PASS"; then
    echo "FULLSTACK_RESULT=PASS"
else
    echo "FULLSTACK_RESULT=FAIL"
fi
