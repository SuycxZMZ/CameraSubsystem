#!/bin/sh
set -eu

PUBLISHER_BIN="${PUBLISHER_BIN:-/home/luckfox/camera_publisher_example}"
CODEC_BIN="${CODEC_BIN:-/home/luckfox/camera_codec_server}"
DEVICE="${DEVICE:-/dev/video45}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-/home/luckfox/codec_records}"

PUBLISHER_LOG="${PUBLISHER_LOG:-/home/luckfox/publisher_codec.log}"
CODEC_LOG="${CODEC_LOG:-/home/luckfox/codec_server.log}"
PUBLISHER_PID_FILE="${PUBLISHER_PID_FILE:-/home/luckfox/publisher_codec.pid}"
CODEC_PID_FILE="${CODEC_PID_FILE:-/home/luckfox/codec_server.pid}"
CLIENT_PY="${CLIENT_PY:-/tmp/codec_v1_smoke_client.py}"

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
mkdir -p "$OUTPUT_DIR"
chmod +x "$PUBLISHER_BIN" "$CODEC_BIN"

cat > "$CLIENT_PY" <<'PY'
import socket
import time

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
    print(data.decode().strip(), flush=True)

send('{"type":"start_recording","request_id":"t1","stream_id":"usb_camera_0","output_dir":"/home/luckfox/codec_records"}')
time.sleep(3)
send('{"type":"status","request_id":"t2","stream_id":"usb_camera_0"}')
time.sleep(1)
send('{"type":"stop_recording","request_id":"t3","stream_id":"usb_camera_0"}')
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

echo "CODEC_CONTROL"
python3 "$CLIENT_PY"
sleep 1

cleanup
sleep 1
trap - EXIT INT TERM

echo "CODEC_LOG"
tail -120 "$CODEC_LOG" || true
echo "PUBLISHER_LOG"
tail -80 "$PUBLISHER_LOG" || true
echo "RECORD_FILES"
ls -lh "$OUTPUT_DIR" | head -20 || true
