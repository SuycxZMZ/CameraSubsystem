#!/bin/sh
set -eu

# Long-running codec stability test for RK3576
# Records for DURATION seconds and checks for stability

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
PUBLISHER_BIN="${PUBLISHER_BIN:-${REMOTE_ROOT}/bin/camera_publisher_example}"
CODEC_BIN="${CODEC_BIN:-${REMOTE_ROOT}/bin/camera_codec_server}"
DEVICE="${DEVICE:-/dev/video45}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings/stability}"
DURATION="${DURATION:-60}"

PUBLISHER_LOG="${PUBLISHER_LOG:-${REMOTE_ROOT}/logs/publisher_stability.log}"
CODEC_LOG="${CODEC_LOG:-${REMOTE_ROOT}/logs/codec_stability.log}"
PUBLISHER_PID_FILE="${PUBLISHER_PID_FILE:-${REMOTE_ROOT}/run/publisher_stability.pid}"
CODEC_PID_FILE="${CODEC_PID_FILE:-${REMOTE_ROOT}/run/codec_stability.pid}"
CLIENT_PY="${CLIENT_PY:-${REMOTE_ROOT}/tmp/codec_stability_client.py}"

cleanup()
{
    if [ -f "$CODEC_PID_FILE" ]; then
        kill "$(cat "$CODEC_PID_FILE")" 2>/dev/null || true
    fi
    if [ -f "$PUBLISHER_PID_FILE" ]; then
        kill "$(cat "$PUBLISHER_PID_FILE")" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

rm -f "$CONTROL_SOCKET" "$DATA_SOCKET" "$CODEC_SOCKET"
rm -rf "$OUTPUT_DIR" "$PUBLISHER_LOG" "$CODEC_LOG" "$PUBLISHER_PID_FILE" "$CODEC_PID_FILE"
mkdir -p "$OUTPUT_DIR" "$(dirname "$PUBLISHER_LOG")" "$(dirname "$PUBLISHER_PID_FILE")" "$(dirname "$CLIENT_PY")"
chmod +x "$PUBLISHER_BIN" "$CODEC_BIN"

cat > "$CLIENT_PY" <<'PY'
import json
import socket
import sys
import time
import os

duration = int(os.environ.get('DURATION', '60'))
output_dir = os.environ.get('OUTPUT_DIR', '/home/luckfox/CameraSubsystem/recordings/stability')
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/camera_subsystem_codec.sock')

def send(line):
    sock.sendall((line + '\n').encode())
    data = b''
    while not data.endswith(b'\n'):
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    text = data.decode().strip()
    print(text, flush=True)
    return json.loads(text)

# Start recording
print(f'START: Beginning {duration} second recording test', flush=True)
start = send(json.dumps({
    "type": "start_recording",
    "request_id": "t1",
    "stream_id": "usb_camera_0",
    "output_dir": output_dir,
}))

if not start.get('recording') or start.get('state') != 'recording':
    print('FAIL: start_recording did not enter recording state', flush=True)
    sys.exit(1)

# Monitor during recording
start_time = time.time()
last_input_frames = 0
last_encoded_frames = 0
stall_count = 0
max_stall = 0

check_interval = 5  # Check every 5 seconds
checks = 0

while time.time() - start_time < duration:
    time.sleep(check_interval)
    checks += 1
    elapsed = time.time() - start_time
    
    status = send('{"type":"status","request_id":"check","stream_id":"usb_camera_0"}')
    input_frames = status.get('input_frames', 0)
    encoded_frames = status.get('encoded_frames', 0)
    decoded_frames = status.get('decoded_frames', 0)
    decode_failures = status.get('decode_failures', 0)
    write_failures = status.get('write_failures', 0)
    
    # Check for stalls
    if input_frames == last_input_frames and elapsed > 10:
        stall_count += 1
        max_stall = max(max_stall, check_interval)
    else:
        stall_count = 0
    
    last_input_frames = input_frames
    last_encoded_frames = encoded_frames
    
    print(f'[{elapsed:.0f}s] input={input_frames} decoded={decoded_frames} encoded={encoded_frames} '
          f'decode_fail={decode_failures} write_fail={write_failures} stall_cnt={stall_count}', flush=True)
    
    # Early exit on critical failures
    if decode_failures > 100:
        print(f'FAIL: Too many decode failures ({decode_failures})', flush=True)
        send('{"type":"stop_recording","request_id":"early_stop","stream_id":"usb_camera_0"}')
        sys.exit(1)
    if write_failures > 10:
        print(f'FAIL: Too many write failures ({write_failures})', flush=True)
        send('{"type":"stop_recording","request_id":"early_stop","stream_id":"usb_camera_0"}')
        sys.exit(1)
    if stall_count >= 3:
        print(f'FAIL: Input stalled for {stall_count * check_interval} seconds', flush=True)
        send('{"type":"stop_recording","request_id":"early_stop","stream_id":"usb_camera_0"}')
        sys.exit(1)

# Stop recording
print('STOP: Ending recording', flush=True)
stop = send('{"type":"stop_recording","request_id":"t3","stream_id":"usb_camera_0"}')
sock.close()

# Validate final results
failures = []
final_input = stop.get('input_frames', 0)
final_decoded = stop.get('decoded_frames', 0)
final_encoded = stop.get('encoded_frames', 0)
final_decode_fail = stop.get('decode_failures', 0)
final_write_fail = stop.get('write_failures', 0)

expected_frames = duration * 30  # Assuming 30fps

if final_input < expected_frames * 0.5:
    failures.append(f'input_frames ({final_input}) too low, expected ~{expected_frames}')
if final_decoded < final_input * 0.9:
    failures.append(f'decoded_frames ({final_decoded}) < 90% of input_frames ({final_input})')
if final_encoded < final_decoded * 0.9:
    failures.append(f'encoded_frames ({final_encoded}) < 90% of decoded_frames ({final_decoded})')
if final_decode_fail > final_input * 0.1:
    failures.append(f'decode_failures ({final_decode_fail}) > 10% of input_frames')
if final_write_fail > 10:
    failures.append(f'write_failures ({final_write_fail}) > 10')

if failures:
    print('STABILITY_RESULT=FAIL', flush=True)
    for item in failures:
        print('  - ' + item, flush=True)
    sys.exit(1)

print(f'STABILITY_RESULT=PASS: {final_input} input, {final_decoded} decoded, {final_encoded} encoded', flush=True)
PY

echo "Starting ${DURATION}s stability test..."
echo "PUBLISHER: $PUBLISHER_BIN"
echo "CODEC: $CODEC_BIN"
echo "DEVICE: $DEVICE"
echo "OUTPUT: $OUTPUT_DIR"

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

export DURATION
export OUTPUT_DIR
python3 "$CLIENT_PY"
result=$?

cleanup
sleep 1
trap - EXIT INT TERM

echo ""
echo "=== CODEC LOG (last 50 lines) ==="
tail -50 "$CODEC_LOG" || true
echo ""
echo "=== OUTPUT FILES ==="
ls -lh "$OUTPUT_DIR" | head -20 || true
echo ""
echo "=== FILE SIZES ==="
total_size=0
for f in "$OUTPUT_DIR"/*.h264; do
    if [ -f "$f" ]; then
        size=$(stat -c%s "$f")
        total_size=$((total_size + size))
        echo "$(basename "$f"): $((size / 1024)) KB"
    fi
done
echo "Total: $((total_size / 1024 / 1024)) MB"

if [ $result -eq 0 ]; then
    echo ""
    echo "STABILITY_TEST=PASS"
else
    echo ""
    echo "STABILITY_TEST=FAIL"
fi

exit $result
