#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
HTTP_PORT="${HTTP_PORT:-8080}"
DETECTION_SOCKET="${DETECTION_SOCKET:-/tmp/camera_subsystem_detection.sock}"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)
RUN_STACK_SCRIPT="${PROJECT_ROOT}/scripts/rk3576-run-web-stack.sh"

usage()
{
    cat <<EOF
Usage: $0

最小 P0 回归：
  1. 重启板端 web+detection 调试栈
  2. 查询 gateway /status
  3. 执行 detection stop/start/set_config
  4. 再次查询 /status，确认 metrics 持续增长
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

extract_number()
{
    local json="$1"
    local key="$2"
    printf '%s\n' "${json}" | grep -o "\"${key}\":[0-9][0-9]*" | head -n1 | cut -d: -f2
}

extract_string()
{
    local json="$1"
    local key="$2"
    printf '%s\n' "${json}" | grep -o "\"${key}\":\"[^\"]*\"" | head -n1 | sed -e "s/\"${key}\":\"//" -e 's/"$//'
}

status_json()
{
    curl -fsS --max-time 5 "http://${BOARD_HOST}:${HTTP_PORT}/status"
}

require_contains()
{
    local haystack="$1"
    local needle="$2"
    local message="$3"
    if [[ "${haystack}" != *"${needle}"* ]]; then
        echo "p0_smoke_fail=${message}" >&2
        exit 1
    fi
}

send_control()
{
    local request="$1"
    run_ssh "'${REMOTE_ROOT}/bin/camera_detection_control_client_example' --control-socket '${DETECTION_SOCKET}' --request '${request}'"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

cd "${PROJECT_ROOT}"

ENABLE_DETECTION=1 DETECTION_ALLOW_PROFILE_FAILURE="${DETECTION_ALLOW_PROFILE_FAILURE:-1}" \
    "${RUN_STACK_SCRIPT}" restart

sleep 3

status_before="$(status_json)"
require_contains "${status_before}" '"detection":{' "gateway_status_missing_detection"
require_contains "${status_before}" '"available":true' "detection_not_available_after_restart"

input_before="$(extract_number "${status_before}" "input_frames")"
infer_before="$(extract_number "${status_before}" "inferred_frames")"
state_before="$(extract_string "${status_before}" "state")"

if [[ -z "${input_before}" || -z "${infer_before}" ]]; then
    echo "p0_smoke_fail=missing_detection_metrics" >&2
    exit 1
fi

send_control '{"type":"stop_detection","request_id":"p0-stop-1","stream_id":"default0"}' >/tmp/detection_p0_stop.log
sleep 2
status_stopped="$(status_json)"
require_contains "${status_stopped}" '"state":"idle"' "detection_not_idle_after_stop"

send_control '{"type":"start_detection","request_id":"p0-start-1","stream_id":"default0"}' >/tmp/detection_p0_start.log
sleep 3
status_started="$(status_json)"
require_contains "${status_started}" '"state":"running"' "detection_not_running_after_start"

send_control '{"type":"set_detection_config","request_id":"p0-config-1","stream_id":"default0","infer_every_n_frames":2,"score_threshold":0.35,"nms_threshold":0.4}' >/tmp/detection_p0_config.log
sleep 2
status_configured="$(status_json)"
require_contains "${status_configured}" '"infer_every_n_frames":2' "infer_every_n_frames_not_applied"
require_contains "${status_configured}" '"score_threshold":0.35' "score_threshold_not_applied"
require_contains "${status_configured}" '"nms_threshold":0.4' "nms_threshold_not_applied"

sleep 3
status_after="$(status_json)"
input_after="$(extract_number "${status_after}" "input_frames")"
infer_after="$(extract_number "${status_after}" "inferred_frames")"

if [[ -z "${input_after}" || -z "${infer_after}" ]]; then
    echo "p0_smoke_fail=missing_detection_metrics_after" >&2
    exit 1
fi

if (( input_after <= input_before )); then
    echo "p0_smoke_fail=input_frames_not_increasing before=${input_before} after=${input_after}" >&2
    exit 1
fi

if (( infer_after <= 0 )); then
    echo "p0_smoke_fail=inferred_frames_not_positive after=${infer_after}" >&2
    exit 1
fi

echo "p0_smoke_result=PASS"
echo "state_before=${state_before:-unknown}"
echo "input_before=${input_before}"
echo "infer_before=${infer_before}"
echo "input_after=${input_after}"
echo "infer_after=${infer_after}"
