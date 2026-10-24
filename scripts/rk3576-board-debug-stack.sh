#!/usr/bin/env bash

set -euo pipefail

REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
DEVICE="${DEVICE:-/dev/video45}"
STREAM_ID="${STREAM_ID:-default0}"
DETECTION_STREAM_ID="${DETECTION_STREAM_ID:-${STREAM_ID}}"
HTTP_PORT="${HTTP_PORT:-8080}"
MAX_PREVIEW_FPS="${MAX_PREVIEW_FPS:-30}"
ENABLE_DETECTION="${ENABLE_DETECTION:-1}"
DETECTION_NPU_CORE_MASK="${DETECTION_NPU_CORE_MASK:-1}"
DETECTION_INFER_EVERY_N_FRAMES="${DETECTION_INFER_EVERY_N_FRAMES:-1}"
DETECTION_SCORE_THRESHOLD="${DETECTION_SCORE_THRESHOLD:-0.25}"
DETECTION_NMS_THRESHOLD="${DETECTION_NMS_THRESHOLD:-0.45}"
DETECTION_PERFORMANCE_PROFILE="${DETECTION_PERFORMANCE_PROFILE:-npu-cpu}"
DETECTION_ALLOW_PROFILE_FAILURE="${DETECTION_ALLOW_PROFILE_FAILURE:-1}"

CONTROL_SOCKET="${CONTROL_SOCKET:-/tmp/camera_subsystem_control.sock}"
DATA_SOCKET="${DATA_SOCKET:-/tmp/camera_subsystem_data.sock}"
CODEC_SOCKET="${CODEC_SOCKET:-/tmp/camera_subsystem_codec.sock}"
DETECTION_SOCKET="${DETECTION_SOCKET:-/tmp/camera_subsystem_detection.sock}"
DETECTION_RESULT_SOCKET="${DETECTION_RESULT_SOCKET:-/tmp/camera_subsystem_detection_result.sock}"
OUTPUT_DIR="${OUTPUT_DIR:-${REMOTE_ROOT}/recordings}"
STATIC_ROOT="${STATIC_ROOT:-${REMOTE_ROOT}/web_preview/dist}"
MODEL_DIR="${MODEL_DIR:-${REMOTE_ROOT}/models}"
DETECTION_MODEL_PATH="${DETECTION_MODEL_PATH:-${MODEL_DIR}/yolo11.rknn}"
DETECTION_LABELS_PATH="${DETECTION_LABELS_PATH:-${MODEL_DIR}/coco_80_labels_list.txt}"

BIN_DIR="${REMOTE_ROOT}/bin"
LOG_DIR="${REMOTE_ROOT}/logs"
RUN_DIR="${REMOTE_ROOT}/run"
TMP_DIR="${REMOTE_ROOT}/tmp"

PUBLISHER_BIN="${BIN_DIR}/camera_publisher_example"
CODEC_BIN="${BIN_DIR}/camera_codec_server"
GATEWAY_BIN="${BIN_DIR}/web_preview_gateway"
DETECTION_BIN="${BIN_DIR}/camera_detection_server"

usage()
{
    cat <<EOF
Usage: $0 <start|stop|restart|status|logs|clean>

Environment:
  REMOTE_ROOT=${REMOTE_ROOT}
  DEVICE=${DEVICE}
  STREAM_ID=${STREAM_ID}
  DETECTION_STREAM_ID=${DETECTION_STREAM_ID}
  HTTP_PORT=${HTTP_PORT}
  MAX_PREVIEW_FPS=${MAX_PREVIEW_FPS}
  ENABLE_DETECTION=${ENABLE_DETECTION}
EOF
}

wait_file()
{
    local path="$1"
    local timeout_sec="${2:-10}"
    local retries=$((timeout_sec * 5))
    local i
    for ((i = 0; i < retries; ++i)); do
        if [[ -e "${path}" ]]; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

wait_http_ready()
{
    local url="$1"
    local timeout_sec="${2:-10}"
    local retries=$((timeout_sec * 5))
    local i
    for ((i = 0; i < retries; ++i)); do
        if curl -fsS --max-time 2 "${url}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

start_background()
{
    local pidfile="$1"
    local logfile="$2"
    shift 2

    setsid "$@" >"${logfile}" 2>&1 < /dev/null &
    echo $! > "${pidfile}"
}

wait_process_exit()
{
    local pattern="$1"
    local timeout_sec="${2:-10}"
    local retries=$((timeout_sec * 5))
    local i
    for ((i = 0; i < retries; ++i)); do
        if ! pgrep -f "${pattern}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

stop_stack()
{
    set +e
    for pidfile in \
        "${RUN_DIR}/gateway.pid" \
        "${RUN_DIR}/detection.pid" \
        "${RUN_DIR}/codec.pid" \
        "${RUN_DIR}/publisher.pid"; do
        if [[ -f "${pidfile}" ]]; then
            kill "$(cat "${pidfile}")" 2>/dev/null || true
        fi
    done
    sleep 1
    pkill -f '[w]eb_preview_gateway' 2>/dev/null || true
    pkill -f '[c]amera_detection_server' 2>/dev/null || true
    pkill -f '[c]amera_codec_server' 2>/dev/null || true
    pkill -f '[c]amera_publisher_example' 2>/dev/null || true
    wait_process_exit '[w]eb_preview_gateway' 10 || echo "warning=web_preview_gateway_exit_timeout" >&2
    wait_process_exit '[c]amera_detection_server' 10 || echo "warning=camera_detection_server_exit_timeout" >&2
    wait_process_exit '[c]amera_codec_server' 10 || echo "warning=camera_codec_server_exit_timeout" >&2
    wait_process_exit '[c]amera_publisher_example' 10 || echo "warning=camera_publisher_example_exit_timeout" >&2
    rm -f \
        "${RUN_DIR}/gateway.pid" \
        "${RUN_DIR}/detection.pid" \
        "${RUN_DIR}/codec.pid" \
        "${RUN_DIR}/publisher.pid" \
        "${CONTROL_SOCKET}" \
        "${DATA_SOCKET}" \
        "${CODEC_SOCKET}" \
        "${DETECTION_SOCKET}" \
        "${DETECTION_RESULT_SOCKET}"
}

clean_runtime()
{
    mkdir -p "${LOG_DIR}" "${RUN_DIR}" "${TMP_DIR}" "${OUTPUT_DIR}" "${MODEL_DIR}"
    rm -f \
        "${LOG_DIR}/publisher.log" \
        "${LOG_DIR}/codec_server.log" \
        "${LOG_DIR}/web_preview_gateway.log" \
        "${LOG_DIR}/camera_detection_server.log"
}

start_stack()
{
    mkdir -p "${BIN_DIR}" "${LOG_DIR}" "${RUN_DIR}" "${TMP_DIR}" "${OUTPUT_DIR}" "${STATIC_ROOT}" \
        "${MODEL_DIR}"

    for bin in "${PUBLISHER_BIN}" "${CODEC_BIN}" "${GATEWAY_BIN}"; do
        if [[ ! -x "${bin}" ]]; then
            echo "missing_executable=${bin}" >&2
            exit 1
        fi
    done
    if [[ "${ENABLE_DETECTION}" == "1" ]]; then
        if [[ ! -x "${DETECTION_BIN}" ]]; then
            echo "missing_executable=${DETECTION_BIN}" >&2
            exit 1
        fi
        if [[ ! -f "${DETECTION_MODEL_PATH}" ]]; then
            echo "missing_model=${DETECTION_MODEL_PATH}" >&2
            exit 1
        fi
        if [[ ! -f "${DETECTION_LABELS_PATH}" ]]; then
            echo "missing_labels=${DETECTION_LABELS_PATH}" >&2
            exit 1
        fi
    fi

    stop_stack
    clean_runtime

    start_background "${RUN_DIR}/publisher.pid" "${LOG_DIR}/publisher.log" \
        "${PUBLISHER_BIN}" "${DEVICE}" "${CONTROL_SOCKET}" "${DATA_SOCKET}" --io-method mmap
    wait_file "${CONTROL_SOCKET}" 10 || {
        echo "publisher_control_socket_timeout=${CONTROL_SOCKET}" >&2
        exit 1
    }
    wait_file "${DATA_SOCKET}" 10 || {
        echo "publisher_data_socket_timeout=${DATA_SOCKET}" >&2
        exit 1
    }

    start_background "${RUN_DIR}/codec.pid" "${LOG_DIR}/codec_server.log" \
        "${CODEC_BIN}" \
        --control-socket "${CONTROL_SOCKET}" \
        --data-socket "${DATA_SOCKET}" \
        --codec-socket "${CODEC_SOCKET}" \
        --output-dir "${OUTPUT_DIR}" \
        --device "${DEVICE}" \
        --stream-id "${STREAM_ID}"
    wait_file "${CODEC_SOCKET}" 10 || {
        echo "codec_socket_timeout=${CODEC_SOCKET}" >&2
        exit 1
    }

    if [[ "${ENABLE_DETECTION}" == "1" ]]; then
        start_background "${RUN_DIR}/detection.pid" "${LOG_DIR}/camera_detection_server.log" \
            "${DETECTION_BIN}" \
            --control-socket "${DETECTION_SOCKET}" \
            --result-socket "${DETECTION_RESULT_SOCKET}" \
            --camera-control-socket "${CONTROL_SOCKET}" \
            --camera-data-socket "${DATA_SOCKET}" \
            --stream-id "${DETECTION_STREAM_ID}" \
            --device "${DEVICE}" \
            --model-path "${DETECTION_MODEL_PATH}" \
            --labels-path "${DETECTION_LABELS_PATH}" \
            --npu-core-mask "${DETECTION_NPU_CORE_MASK}" \
            --infer-every-n-frames "${DETECTION_INFER_EVERY_N_FRAMES}" \
            --score-threshold "${DETECTION_SCORE_THRESHOLD}" \
            --nms-threshold "${DETECTION_NMS_THRESHOLD}" \
            --performance-profile "${DETECTION_PERFORMANCE_PROFILE}" \
            --allow-performance-profile-failure "${DETECTION_ALLOW_PROFILE_FAILURE}"
        wait_file "${DETECTION_SOCKET}" 10 || {
            echo "detection_socket_timeout=${DETECTION_SOCKET}" >&2
            exit 1
        }
        wait_file "${DETECTION_RESULT_SOCKET}" 10 || {
            echo "detection_result_socket_timeout=${DETECTION_RESULT_SOCKET}" >&2
            exit 1
        }
    fi

    start_background "${RUN_DIR}/gateway.pid" "${LOG_DIR}/web_preview_gateway.log" \
        "${GATEWAY_BIN}" \
        --control-socket "${CONTROL_SOCKET}" \
        --data-socket "${DATA_SOCKET}" \
        --codec-socket "${CODEC_SOCKET}" \
        --detection-socket "${DETECTION_SOCKET}" \
        --output-dir "${OUTPUT_DIR}" \
        --device "${DEVICE}" \
        --stream-id "${STREAM_ID}" \
        --static-root "${STATIC_ROOT}" \
        --port "${HTTP_PORT}" \
        --max-fps "${MAX_PREVIEW_FPS}"
    wait_http_ready "http://127.0.0.1:${HTTP_PORT}/status" 10 || {
        echo "gateway_http_ready_timeout=http://127.0.0.1:${HTTP_PORT}/status" >&2
        exit 1
    }

    echo "publisher_pid=$(cat "${RUN_DIR}/publisher.pid")"
    echo "codec_pid=$(cat "${RUN_DIR}/codec.pid")"
    if [[ -f "${RUN_DIR}/detection.pid" ]]; then
        echo "detection_pid=$(cat "${RUN_DIR}/detection.pid")"
    fi
    echo "gateway_pid=$(cat "${RUN_DIR}/gateway.pid")"
    echo "web_url=http://$(hostname -I 2>/dev/null | awk '{print $1}'):${HTTP_PORT}"
}

show_status()
{
    set +e
    echo "processes:"
    pgrep -af '[c]amera_publisher_example|[c]amera_codec_server|[c]amera_detection_server|[w]eb_preview_gateway' || true
    echo
    echo "sockets:"
    ls -l "${CONTROL_SOCKET}" "${DATA_SOCKET}" "${CODEC_SOCKET}" "${DETECTION_SOCKET}" \
        "${DETECTION_RESULT_SOCKET}" 2>/dev/null || true
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
        "${LOG_DIR}/camera_detection_server.log" \
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
