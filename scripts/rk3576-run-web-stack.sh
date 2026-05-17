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

remote_script="${REMOTE_ROOT}/scripts/rk3576-board-debug-stack.sh"

usage()
{
    cat <<EOF
Usage: $0 <start|stop|restart|status|logs|clean>

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

remote_exec_cmd()
{
    local action="$1"
    cat <<EOF
set -e
if [ ! -x '${remote_script}' ]; then
  echo "missing_remote_script=${remote_script}" >&2
  exit 1
fi
REMOTE_ROOT='${REMOTE_ROOT}' \
DEVICE='${DEVICE}' \
STREAM_ID='${STREAM_ID}' \
HTTP_PORT='${HTTP_PORT}' \
MAX_PREVIEW_FPS='${MAX_PREVIEW_FPS}' \
CONTROL_SOCKET='${CONTROL_SOCKET}' \
DATA_SOCKET='${DATA_SOCKET}' \
CODEC_SOCKET='${CODEC_SOCKET}' \
OUTPUT_DIR='${OUTPUT_DIR}' \
STATIC_ROOT='${STATIC_ROOT}' \
'${remote_script}' '${action}'
EOF
}

action="${1:-}"
case "${action}" in
    start)
        run_ssh "$(remote_exec_cmd start)"
        ;;
    stop)
        run_ssh "$(remote_exec_cmd stop)"
        ;;
    restart)
        run_ssh "$(remote_exec_cmd restart)"
        ;;
    status)
        run_ssh "$(remote_exec_cmd status)"
        ;;
    logs)
        run_ssh "$(remote_exec_cmd logs)"
        ;;
    clean)
        run_ssh "$(remote_exec_cmd clean)"
        ;;
    -h|--help|help|"")
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
