#!/usr/bin/env bash

set -euo pipefail

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
DEVICE="${DEVICE:-/dev/video45}"
STREAM_ID="${STREAM_ID:-0}"
HTTP_PORT="${HTTP_PORT:-8080}"
MAX_PREVIEW_FPS="${MAX_PREVIEW_FPS:-30}"

CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings}"
STATIC_ROOT="${STATIC_ROOT:-${REMOTE_ROOT}/web_preview/dist}"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

remote_bin="${REMOTE_ROOT}/bin"
remote_logs="${REMOTE_ROOT}/logs"
remote_run="${REMOTE_ROOT}/run"

usage()
{
    cat <<EOF
Usage: $0 <start|stop|restart|status|logs>

Environment:
  BOARD_HOST=${BOARD_HOST}
  BOARD_USER=${BOARD_USER}
  REMOTE_ROOT=${REMOTE_ROOT}
  DEVICE=${DEVICE}
  STREAM_ID=${STREAM_ID}
  HTTP_PORT=${HTTP_PORT}
EOF
}

run_ssh()
{
    local command="$1"
    if command -v sshpass >/dev/null 2>&1; then
        sshpass -p "${BOARD_PASSWORD}" ssh "${SSH_OPTS[@]}" "${TARGET}" "${command}"
    elif command -v expect >/dev/null 2>&1; then
        EXPECT_TARGET="${TARGET}" EXPECT_PASSWORD="${BOARD_PASSWORD}" EXPECT_COMMAND="${command}" \
        expect -c '
            set timeout -1
            set target $env(EXPECT_TARGET)
            set password $env(EXPECT_PASSWORD)
            set command $env(EXPECT_COMMAND)
            spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $target $command
            expect {
                -re "(?i)password:" {
                    send "$password\r"
                    exp_continue
                }
                eof
            }
            catch wait result
            exit [lindex $result 3]
        '
    else
        ssh "${SSH_OPTS[@]}" "${TARGET}" "${command}"
    fi
}

remote_stop_cmd()
{
    cat <<EOF
set +e
for pidfile in \
  '${remote_run}/gateway.pid' \
  '${remote_run}/codec.pid' \
  '${remote_run}/publisher.pid'; do
    if [ -f "\${pidfile}" ]; then
        kill "\$(cat "\${pidfile}")" 2>/dev/null || true
    fi
done
sleep 1
pkill -f '[w]eb_preview_gateway' 2>/dev/null || true
pkill -f '[c]amera_codec_server' 2>/dev/null || true
pkill -f '[c]amera_publisher_example' 2>/dev/null || true
rm -f '${remote_run}/gateway.pid' '${remote_run}/codec.pid' '${remote_run}/publisher.pid'
rm -f '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${CODEC_SOCKET}'
EOF
}

remote_start_cmd()
{
    cat <<EOF
set -e
mkdir -p '${remote_logs}' '${remote_run}' '${OUTPUT_DIR}' '${STATIC_ROOT}'
$(remote_stop_cmd)

'${remote_bin}/camera_publisher_example' '${DEVICE}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' --io-method mmap \
  > '${remote_logs}/publisher.log' 2>&1 &
echo \$! > '${remote_run}/publisher.pid'
sleep 1

'${remote_bin}/camera_codec_server' \
  --control-socket '${CONTROL_SOCKET}' \
  --data-socket '${DATA_SOCKET}' \
  --codec-socket '${CODEC_SOCKET}' \
  --output-dir '${OUTPUT_DIR}' \
  --device '${DEVICE}' \
  --stream-id '${STREAM_ID}' \
  > '${remote_logs}/codec_server.log' 2>&1 &
echo \$! > '${remote_run}/codec.pid'
sleep 1

'${remote_bin}/web_preview_gateway' \
  --control-socket '${CONTROL_SOCKET}' \
  --data-socket '${DATA_SOCKET}' \
  --codec-socket '${CODEC_SOCKET}' \
  --output-dir '${OUTPUT_DIR}' \
  --device '${DEVICE}' \
  --stream-id '${STREAM_ID}' \
  --static-root '${STATIC_ROOT}' \
  --port '${HTTP_PORT}' \
  --max-fps '${MAX_PREVIEW_FPS}' \
  > '${remote_logs}/web_preview_gateway.log' 2>&1 &
echo \$! > '${remote_run}/gateway.pid'
sleep 1

echo "publisher_pid=\$(cat '${remote_run}/publisher.pid')"
echo "codec_pid=\$(cat '${remote_run}/codec.pid')"
echo "gateway_pid=\$(cat '${remote_run}/gateway.pid')"
echo "web_url=http://${BOARD_HOST}:${HTTP_PORT}"
EOF
}

remote_status_cmd()
{
    cat <<EOF
set +e
echo "processes:"
pgrep -af '[c]amera_publisher_example|[c]amera_codec_server|[w]eb_preview_gateway' || true
echo
echo "sockets:"
ls -l '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${CODEC_SOCKET}' 2>/dev/null || true
echo
echo "http:"
curl -s --max-time 2 "http://127.0.0.1:${HTTP_PORT}/status" 2>/dev/null || true
echo
echo
echo "listen:"
ss -lntp 2>/dev/null | grep ":${HTTP_PORT}" || true
EOF
}

remote_logs_cmd()
{
    cat <<EOF
set +e
for log in \
  '${remote_logs}/publisher.log' \
  '${remote_logs}/codec_server.log' \
  '${remote_logs}/web_preview_gateway.log'; do
    echo "===== \${log} ====="
    tail -80 "\${log}" 2>/dev/null || true
done
EOF
}

action="${1:-}"
case "${action}" in
    start)
        run_ssh "$(remote_start_cmd)"
        ;;
    stop)
        run_ssh "$(remote_stop_cmd)"
        ;;
    restart)
        run_ssh "$(remote_start_cmd)"
        ;;
    status)
        run_ssh "$(remote_status_cmd)"
        ;;
    logs)
        run_ssh "$(remote_logs_cmd)"
        ;;
    -h|--help|help|"")
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
