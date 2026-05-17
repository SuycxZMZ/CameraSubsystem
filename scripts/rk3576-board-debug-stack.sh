#!/usr/bin/env bash

set -euo pipefail

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

BIN_DIR="${REMOTE_ROOT}/bin"
LOG_DIR="${REMOTE_ROOT}/logs"
RUN_DIR="${REMOTE_ROOT}/run"
TMP_DIR="${REMOTE_ROOT}/tmp"

PUBLISHER_BIN="${BIN_DIR}/camera_publisher_example"
CODEC_BIN="${BIN_DIR}/camera_codec_server"
GATEWAY_BIN="${BIN_DIR}/web_preview_gateway"

usage()
{
    cat <<EOF
Usage: $0 <start|stop|restart|status|logs|clean>

Environment:
  REMOTE_ROOT=${REMOTE_ROOT}
  DEVICE=${DEVICE}
  STREAM_ID=${STREAM_ID}
  HTTP_PORT=${HTTP_PORT}
  MAX_PREVIEW_FPS=${MAX_PREVIEW_FPS}
EOF
}

stop_stack()
{
    set +e
    for pidfile in \
        "${RUN_DIR}/gateway.pid" \
        "${RUN_DIR}/codec.pid" \
        "${RUN_DIR}/publisher.pid"; do
        if [[ -f "${pidfile}" ]]; then
            kill "$(cat "${pidfile}")" 2>/dev/null || true
        fi
    done
    sleep 1
    pkill -f '[w]eb_preview_gateway' 2>/dev/null || true
    pkill -f '[c]amera_codec_server' 2>/dev/null || true
    pkill -f '[c]amera_publisher_example' 2>/dev/null || true
    rm -f \
        "${RUN_DIR}/gateway.pid" \
        "${RUN_DIR}/codec.pid" \
        "${RUN_DIR}/publisher.pid" \
        "${CONTROL_SOCKET}" \
        "${DATA_SOCKET}" \
        "${CODEC_SOCKET}"
}

clean_runtime()
{
    mkdir -p "${LOG_DIR}" "${RUN_DIR}" "${TMP_DIR}" "${OUTPUT_DIR}"
    rm -f \
        "${LOG_DIR}/publisher.log" \
        "${LOG_DIR}/codec_server.log" \
        "${LOG_DIR}/web_preview_gateway.log"
}

start_stack()
{
    mkdir -p "${BIN_DIR}" "${LOG_DIR}" "${RUN_DIR}" "${TMP_DIR}" "${OUTPUT_DIR}" "${STATIC_ROOT}"

    for bin in "${PUBLISHER_BIN}" "${CODEC_BIN}" "${GATEWAY_BIN}"; do
        if [[ ! -x "${bin}" ]]; then
            echo "missing_executable=${bin}" >&2
            exit 1
        fi
    done

    stop_stack
    clean_runtime

    "${PUBLISHER_BIN}" "${DEVICE}" "${CONTROL_SOCKET}" "${DATA_SOCKET}" --io-method mmap \
        > "${LOG_DIR}/publisher.log" 2>&1 &
    echo $! > "${RUN_DIR}/publisher.pid"
    sleep 1

    "${CODEC_BIN}" \
        --control-socket "${CONTROL_SOCKET}" \
        --data-socket "${DATA_SOCKET}" \
        --codec-socket "${CODEC_SOCKET}" \
        --output-dir "${OUTPUT_DIR}" \
        --device "${DEVICE}" \
        --stream-id "${STREAM_ID}" \
        > "${LOG_DIR}/codec_server.log" 2>&1 &
    echo $! > "${RUN_DIR}/codec.pid"
    sleep 1

    "${GATEWAY_BIN}" \
        --control-socket "${CONTROL_SOCKET}" \
        --data-socket "${DATA_SOCKET}" \
        --codec-socket "${CODEC_SOCKET}" \
        --output-dir "${OUTPUT_DIR}" \
        --device "${DEVICE}" \
        --stream-id "${STREAM_ID}" \
        --static-root "${STATIC_ROOT}" \
        --port "${HTTP_PORT}" \
        --max-fps "${MAX_PREVIEW_FPS}" \
        > "${LOG_DIR}/web_preview_gateway.log" 2>&1 &
    echo $! > "${RUN_DIR}/gateway.pid"
    sleep 1

    echo "publisher_pid=$(cat "${RUN_DIR}/publisher.pid")"
    echo "codec_pid=$(cat "${RUN_DIR}/codec.pid")"
    echo "gateway_pid=$(cat "${RUN_DIR}/gateway.pid")"
    echo "web_url=http://$(hostname -I 2>/dev/null | awk '{print $1}'):${HTTP_PORT}"
}

show_status()
{
    set +e
    echo "processes:"
    pgrep -af '[c]amera_publisher_example|[c]amera_codec_server|[w]eb_preview_gateway' || true
    echo
    echo "sockets:"
    ls -l "${CONTROL_SOCKET}" "${DATA_SOCKET}" "${CODEC_SOCKET}" 2>/dev/null || true
    echo
    echo "http:"
    curl -s --max-time 2 "http://127.0.0.1:${HTTP_PORT}/status" 2>/dev/null || true
    echo
    echo
    echo "listen:"
    ss -lntp 2>/dev/null | grep ":${HTTP_PORT}" || true
}

show_logs()
{
    set +e
    for log in \
        "${LOG_DIR}/publisher.log" \
        "${LOG_DIR}/codec_server.log" \
        "${LOG_DIR}/web_preview_gateway.log"; do
        echo "===== ${log} ====="
        tail -80 "${log}" 2>/dev/null || true
    done
}

action="${1:-}"
case "${action}" in
    start)
        start_stack
        ;;
    stop)
        stop_stack
        ;;
    restart)
        start_stack
        ;;
    status)
        show_status
        ;;
    logs)
        show_logs
        ;;
    clean)
        stop_stack
        clean_runtime
        ;;
    -h|--help|help|"")
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
