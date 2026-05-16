#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${CAMERA_SUBSYSTEM_RK3576_OUTPUT_DIR:-${PROJECT_ROOT}/bin/rk3576}"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
BOARD_DIR="${BOARD_DIR:-/tmp/camera_subsystem_dmabuf_v2_failover}"
DEVICE="${DEVICE:-/dev/video45}"
PRE_CRASH_SEC="${PRE_CRASH_SEC:-6}"
POST_CRASH_SEC="${POST_CRASH_SEC:-8}"
SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC:-5}"
REQUESTED_FPS="${REQUESTED_FPS:-15}"
FAULT_MODE="${FAULT_MODE:-crash}"
FAULT_RELEASE_DISCONNECT_AFTER_FRAMES="${FAULT_RELEASE_DISCONNECT_AFTER_FRAMES:-5}"
CRASH_RELEASE_DELAY_MS="${CRASH_RELEASE_DELAY_MS:-700}"
NORMAL_PROCESS_DELAY_MS="${NORMAL_PROCESS_DELAY_MS:-5}"
SKIP_BUILD="${SKIP_BUILD:-0}"
MIN_NORMAL_FRAMES="${MIN_NORMAL_FRAMES:-10}"
MIN_CRASHED_FRAMES="${MIN_CRASHED_FRAMES:-3}"
MIN_FAULT_RELEASE_FAIL="${MIN_FAULT_RELEASE_FAIL:-1}"
MAX_RELEASE_PENDING="${MAX_RELEASE_PENDING:-0}"
MAX_ACTIVE_LEASES="${MAX_ACTIVE_LEASES:-0}"
MAX_CAPTURE_DROPPED="${MAX_CAPTURE_DROPPED:--1}"
MAX_RELEASE_TIMEOUT="${MAX_RELEASE_TIMEOUT:--1}"
MAX_LEASE_EXHAUSTED="${MAX_LEASE_EXHAUSTED:--1}"
MAX_EXPORT_FAIL="${MAX_EXPORT_FAIL:-0}"

CONTROL_SOCKET="/tmp/camera_subsystem_control.sock"
DATA_SOCKET="/tmp/camera_subsystem_data_v2.sock"
RELEASE_SOCKET="/tmp/camera_subsystem_release_v2.sock"

LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-dataplane-v2-failover-smoke}"
TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

if [[ "${FAULT_MODE}" != "crash" && "${FAULT_MODE}" != "release-disconnect" ]]; then
    echo "FAULT_MODE must be 'crash' or 'release-disconnect'"
    exit 1
fi

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

run_scp()
{
    if command -v sshpass >/dev/null 2>&1; then
        sshpass -p "${BOARD_PASSWORD}" scp "${SSH_OPTS[@]}" "$@"
    elif command -v expect >/dev/null 2>&1; then
        local scp_args
        scp_args="$*"
        EXPECT_PASSWORD="${BOARD_PASSWORD}" EXPECT_SCP_ARGS="${scp_args}" \
        expect -c '
            set timeout -1
            set password $env(EXPECT_PASSWORD)
            set scp_args [split $env(EXPECT_SCP_ARGS)]
            spawn scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null {*}$scp_args
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
        scp "${SSH_OPTS[@]}" "$@"
    fi
}

extract_counter()
{
    local line="$1"
    local key="$2"
    sed -n "s/.*${key}=\\([0-9][0-9]*\\).*/\\1/p" <<<"${line}"
}

check_eq()
{
    local label="$1"
    local actual="$2"
    local expected="$3"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "FAIL: ${label}: expected ${expected}, got ${actual}"
        return 1
    fi
    echo "PASS: ${label}=${actual}"
}

check_le()
{
    local label="$1"
    local actual="$2"
    local max="$3"
    if (( actual > max )); then
        echo "FAIL: ${label}: expected <= ${max}, got ${actual}"
        return 1
    fi
    echo "PASS: ${label}=${actual} <= ${max}"
}

check_ge()
{
    local label="$1"
    local actual="$2"
    local min="$3"
    if (( actual < min )); then
        echo "FAIL: ${label}: expected >= ${min}, got ${actual}"
        return 1
    fi
    echo "PASS: ${label}=${actual} >= ${min}"
}

if [[ "${SKIP_BUILD}" != "1" ]]; then
    "${PROJECT_ROOT}/scripts/build-rk3576.sh"
fi

mkdir -p "${LOCAL_LOG_DIR}"

fault_extra_args=""
if [[ "${FAULT_MODE}" == "release-disconnect" ]]; then
    fault_extra_args="--release-disconnect-after-frames ${FAULT_RELEASE_DISCONNECT_AFTER_FRAMES}"
fi

run_ssh "mkdir -p '${BOARD_DIR}'"
run_scp \
    "${OUTPUT_DIR}/camera_publisher_example" \
    "${OUTPUT_DIR}/camera_subscriber_example" \
    "${TARGET}:${BOARD_DIR}/" >/dev/null

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    pkill -f '[c]amera_publisher_example' 2>/dev/null || true; \
    pkill -f '[c]amera_subscriber_example' 2>/dev/null || true; \
    rm -f '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${RELEASE_SOCKET}'; \
    rm -f publisher.log subscriber-normal.log subscriber-crash.log metrics_history.jsonl metrics_snapshot.json *.pid; \
    mkdir -p normal_frames crash_frames"

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    nohup ./camera_publisher_example '${DEVICE}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' \
        --io-method dmabuf --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --metrics-history-path '${BOARD_DIR}/metrics_history.jsonl' \
        --metrics-history-interval '${SAMPLE_INTERVAL_SEC}' \
        --metrics-snapshot-path '${BOARD_DIR}/metrics_snapshot.json' \
        > publisher.log 2>&1 & echo \$! > publisher.pid"

sleep 2

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    nohup ./camera_subscriber_example normal_frames '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${DEVICE}' \
        --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --process-delay-ms '${NORMAL_PROCESS_DELAY_MS}' --release-delay-ms 0 \
        > subscriber-normal.log 2>&1 & echo \$! > subscriber-normal.pid; \
    nohup ./camera_subscriber_example crash_frames '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${DEVICE}' \
        --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --process-delay-ms 5 --release-delay-ms '${CRASH_RELEASE_DELAY_MS}' ${fault_extra_args} \
        > subscriber-crash.log 2>&1 & echo \$! > subscriber-crash.pid"

if [[ "${FAULT_MODE}" == "crash" ]]; then
    sleep "${PRE_CRASH_SEC}"

    run_ssh "set +e; \
        cd '${BOARD_DIR}'; \
        kill -9 \$(cat subscriber-crash.pid 2>/dev/null) 2>/dev/null; \
        true"

    sleep "${POST_CRASH_SEC}"
else
    sleep $((PRE_CRASH_SEC + POST_CRASH_SEC))
fi

run_ssh "set +e; \
    cd '${BOARD_DIR}'; \
    kill \$(cat subscriber-normal.pid 2>/dev/null) 2>/dev/null; \
    kill \$(cat subscriber-crash.pid 2>/dev/null) 2>/dev/null; \
    sleep 2; \
    kill \$(cat publisher.pid 2>/dev/null) 2>/dev/null; \
    sleep 1; \
    pkill -f '[c]amera_publisher_example' 2>/dev/null; \
    pkill -f '[c]amera_subscriber_example' 2>/dev/null; \
    true"

run_scp \
    "${TARGET}:${BOARD_DIR}/publisher.log" \
    "${TARGET}:${BOARD_DIR}/subscriber-normal.log" \
    "${TARGET}:${BOARD_DIR}/subscriber-crash.log" \
    "${LOCAL_LOG_DIR}/" >/dev/null

run_scp \
    "${TARGET}:${BOARD_DIR}/metrics_history.jsonl" \
    "${TARGET}:${BOARD_DIR}/metrics_snapshot.json" \
    "${LOCAL_LOG_DIR}/" >/dev/null || true

echo "Logs copied to ${LOCAL_LOG_DIR}"
echo
echo "Publisher metrics:"
tail -n 3 "${LOCAL_LOG_DIR}/metrics_history.jsonl" 2>/dev/null || true
echo
echo "Normal subscriber summary:"
grep -E "summary|release_fail|fps=" "${LOCAL_LOG_DIR}/subscriber-normal.log" | tail -n 10 || true
echo
echo "Fault subscriber last counters:"
grep -E "sec=|summary|release_fail|fps=" "${LOCAL_LOG_DIR}/subscriber-crash.log" | tail -n 10 || true

normal_summary="$(grep -E "summary:" "${LOCAL_LOG_DIR}/subscriber-normal.log" | tail -n 1 || true)"
crashed_line="$(grep -E "sec=" "${LOCAL_LOG_DIR}/subscriber-crash.log" | tail -n 1 || true)"
crashed_summary="$(grep -E "summary:" "${LOCAL_LOG_DIR}/subscriber-crash.log" | tail -n 1 || true)"

echo
echo "Automatic failover checks:"

failures=0

metrics_history_local="${LOCAL_LOG_DIR}/metrics_history.jsonl"
metrics_snapshot_local="${LOCAL_LOG_DIR}/metrics_snapshot.json"
duration_sec=$((PRE_CRASH_SEC + POST_CRASH_SEC))

py_args=("${PROJECT_ROOT}/scripts/metrics_smoke_evaluator.py")
py_args+=(--history "${metrics_history_local}")
if [[ -s "${metrics_snapshot_local}" ]]; then
    py_args+=(--snapshot "${metrics_snapshot_local}")
fi
py_args+=(
    --tier "${TIER:-full}"
    --output-report "${LOCAL_LOG_DIR}/metrics_smoke_report.json"
    --device "${DEVICE}"
    --duration-sec "${duration_sec}"
    --subscriber-count 2
    --requested-fps "${REQUESTED_FPS}"
)

set +e
METRICS_MAX_RELEASE_PENDING="${MAX_RELEASE_PENDING}" \
METRICS_MAX_ACTIVE_LEASES="${MAX_ACTIVE_LEASES}" \
METRICS_MAX_CAPTURE_DROPPED="${MAX_CAPTURE_DROPPED}" \
METRICS_MAX_RELEASE_TIMEOUT="${MAX_RELEASE_TIMEOUT}" \
METRICS_MAX_LEASE_EXHAUSTED="${MAX_LEASE_EXHAUSTED}" \
METRICS_MIN_CAPTURE_FRAMES="${METRICS_MIN_CAPTURE_FRAMES:-1}" \
METRICS_MIN_V2_SENT="${METRICS_MIN_V2_SENT:-1}" \
    python3 "${py_args[@]}"
metrics_exit=$?
set -e
if (( metrics_exit == 0 )); then
    echo "PASS: metrics evaluator"
elif (( metrics_exit == 1 )); then
    echo "FAIL: metrics evaluator detected threshold breach"
    failures=$((failures + 1))
elif (( metrics_exit == 2 )); then
    echo "ERROR: metrics evaluator encountered input/environment error (exit 2)"
    failures=$((failures + 1))
else
    echo "ERROR: metrics evaluator unexpected exit code ${metrics_exit}"
    failures=$((failures + 1))
fi

if [[ -z "${normal_summary}" ]]; then
    echo "FAIL: normal subscriber summary not found"
    failures=$((failures + 1))
else
    normal_frames="$(extract_counter "${normal_summary}" "frames")"
    normal_save_fail="$(extract_counter "${normal_summary}" "save_fail")"
    normal_release_fail="$(extract_counter "${normal_summary}" "release_fail")"
    check_ge "normal.frames" "${normal_frames:-0}" "${MIN_NORMAL_FRAMES}" || failures=$((failures + 1))
    check_eq "normal.save_fail" "${normal_save_fail:-missing}" "0" || failures=$((failures + 1))
    check_eq "normal.release_fail" "${normal_release_fail:-missing}" "0" || failures=$((failures + 1))
fi

if [[ "${FAULT_MODE}" == "release-disconnect" ]]; then
    if [[ -z "${crashed_summary}" ]]; then
        echo "FAIL: fault subscriber summary not found"
        failures=$((failures + 1))
    else
        fault_frames="$(extract_counter "${crashed_summary}" "frames")"
        fault_release_fail="$(extract_counter "${crashed_summary}" "release_fail")"
        check_ge "fault.frames" "${fault_frames:-0}" "${MIN_CRASHED_FRAMES}" || failures=$((failures + 1))
        check_ge "fault.release_fail_after_disconnect" "${fault_release_fail:-0}" "${MIN_FAULT_RELEASE_FAIL}" || failures=$((failures + 1))
    fi
elif [[ -z "${crashed_line}" ]]; then
    echo "FAIL: fault subscriber received no frames before kill"
    failures=$((failures + 1))
else
    crashed_frames="$(extract_counter "${crashed_line}" "frames")"
    crashed_release_fail="$(extract_counter "${crashed_line}" "release_fail")"
    check_ge "fault.frames" "${crashed_frames:-0}" "${MIN_CRASHED_FRAMES}" || failures=$((failures + 1))
    check_eq "fault.release_fail_before_kill" "${crashed_release_fail:-missing}" "0" || failures=$((failures + 1))
fi

if (( failures > 0 )); then
    echo
    echo "rk3576_dataplane_v2_failover_result=FAIL failures=${failures}"
    exit 1
fi

echo
echo "rk3576_dataplane_v2_failover_result=PASS"
