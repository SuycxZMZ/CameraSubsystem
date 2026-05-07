#!/bin/sh
set -eu

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
PUBLISHER_BIN="${PUBLISHER_BIN:-${REMOTE_ROOT}/bin/camera_publisher_example}"
CODEC_BIN="${CODEC_BIN:-${REMOTE_ROOT}/bin/camera_codec_server}"
DEVICE="${DEVICE:-/dev/video45}"
CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings/mp4_smoke}"
DURATION_SEC="${DURATION_SEC:-5}"
FPS="${FPS:-20}"

PUBLISHER_LOG="${PUBLISHER_LOG:-${REMOTE_ROOT}/logs/publisher_mp4_smoke.log}"
CODEC_LOG="${CODEC_LOG:-${REMOTE_ROOT}/logs/codec_mp4_smoke.log}"
PUBLISHER_PID_FILE="${PUBLISHER_PID_FILE:-${REMOTE_ROOT}/run/publisher_mp4_smoke.pid}"
CODEC_PID_FILE="${CODEC_PID_FILE:-${REMOTE_ROOT}/run/codec_mp4_smoke.pid}"
CLIENT_PY="${CLIENT_PY:-/tmp/codec_mp4_smoke.py}"

cleanup()
{
    for f in "$CODEC_PID_FILE" "$PUBLISHER_PID_FILE"; do
        if [ -f "$f" ]; then
            kill "$(cat "$f")" 2>/dev/null || true
        fi
    done
}

dump_logs()
{
    echo "CODEC_LOG"
    tail -80 "$CODEC_LOG" || true
    echo "PUBLISHER_LOG"
    tail -80 "$PUBLISHER_LOG" || true
    echo "MP4_FILES"
    ls -lh "$OUTPUT_DIR"/*.mp4 2>/dev/null || true
}

trap cleanup EXIT INT TERM

rm -f "$CONTROL_SOCKET" "$DATA_SOCKET" "$CODEC_SOCKET"
rm -rf "$OUTPUT_DIR" "$PUBLISHER_LOG" "$CODEC_LOG" \
       "$PUBLISHER_PID_FILE" "$CODEC_PID_FILE"
mkdir -p "$OUTPUT_DIR" "${REMOTE_ROOT}/logs" "${REMOTE_ROOT}/run"
chmod +x "$PUBLISHER_BIN" "$CODEC_BIN"

cat > "$CLIENT_PY" <<'PY'
import json
import os
import socket
import time

codec_socket = os.environ["CODEC_SOCKET"]
output_dir = os.environ["OUTPUT_DIR"]
duration = float(os.environ.get("DURATION_SEC", "5"))
fps = int(os.environ.get("FPS", "20"))

def send(cmd):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(codec_socket)
    sock.sendall((json.dumps(cmd) + "\n").encode())
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(65536)
        if not chunk:
            break
        data += chunk
    sock.close()
    if not data:
        raise RuntimeError("empty codec response")
    return json.loads(data.decode().strip())

start = send({
    "type": "start_recording",
    "request_id": "mp4-start",
    "stream_id": "0",
    "container": "mp4",
    "output_dir": output_dir,
    "profile": {"fps": fps},
})
print("START " + json.dumps(start, sort_keys=True), flush=True)
if not start.get("recording"):
    raise SystemExit(2)

time.sleep(duration)

status = send({
    "type": "status",
    "request_id": "mp4-status",
    "stream_id": "0",
    "container": "mp4",
})
print("STATUS " + json.dumps(status, sort_keys=True), flush=True)

stop = send({
    "type": "stop_recording",
    "request_id": "mp4-stop",
    "stream_id": "0",
    "container": "mp4",
})
print("STOP " + json.dumps(stop, sort_keys=True), flush=True)

if stop.get("recording"):
    raise SystemExit(3)
if stop.get("state") != "idle":
    raise SystemExit(4)
if stop.get("encoded_frames", 0) <= 0:
    raise SystemExit(5)
if stop.get("decode_failures", 0) != 0:
    raise SystemExit(6)
path = stop.get("file", "")
if not path.endswith(".mp4"):
    raise SystemExit(7)
print("MP4_FILE=" + path, flush=True)
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

set +e
CODEC_SOCKET="$CODEC_SOCKET" OUTPUT_DIR="$OUTPUT_DIR" DURATION_SEC="$DURATION_SEC" FPS="$FPS" \
    python3 "$CLIENT_PY"
client_rc=$?
set -e

cleanup
sleep 1
trap - EXIT INT TERM

dump_logs
exit "$client_rc"
