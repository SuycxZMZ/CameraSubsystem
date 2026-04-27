#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${CAMERA_SUBSYSTEM_RK3576_OUTPUT_DIR:-${PROJECT_ROOT}/bin/rk3576}"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
BOARD_DIR="${BOARD_DIR:-/tmp/camera_subsystem_dmabuf_v2}"
DEVICE="${DEVICE:-/dev/video45}"
DURATION_SEC="${DURATION_SEC:-20}"
SLOW_RELEASE_DELAY_MS="${SLOW_RELEASE_DELAY_MS:-700}"
SLOW_PROCESS_DELAY_MS="${SLOW_PROCESS_DELAY_MS:-20}"
NORMAL_PROCESS_DELAY_MS="${NORMAL_PROCESS_DELAY_MS:-5}"
SKIP_BUILD="${SKIP_BUILD:-0}"
MIN_SUBSCRIBER_FRAMES="${MIN_SUBSCRIBER_FRAMES:-5}"
MAX_RELEASE_TIMEOUT="${MAX_RELEASE_TIMEOUT:-0}"
MAX_V2_SEND_FAIL="${MAX_V2_SEND_FAIL:-2}"

CONTROL_SOCKET="/tmp/camera_subsystem_control.sock"
DATA_SOCKET="/tmp/camera_subsystem_data_v2.sock"
RELEASE_SOCKET="/tmp/camera_subsystem_release_v2.sock"

LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-dataplane-v2-smoke}"
TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

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
    rm -f publisher.log subscriber-normal.log subscriber-slow.log *.pid; \
    mkdir -p normal_frames slow_frames"

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    nohup ./camera_publisher_example '${DEVICE}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' \
        --io-method dmabuf --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        > publisher.log 2>&1 & echo \$! > publisher.pid"

sleep 2

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    nohup ./camera_subscriber_example normal_frames '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${DEVICE}' \
        --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --process-delay-ms '${NORMAL_PROCESS_DELAY_MS}' --release-delay-ms 0 \
        > subscriber-normal.log 2>&1 & echo \$! > subscriber-normal.pid; \
    nohup ./camera_subscriber_example slow_frames '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${DEVICE}' \
        --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --process-delay-ms '${SLOW_PROCESS_DELAY_MS}' --release-delay-ms '${SLOW_RELEASE_DELAY_MS}' \
        > subscriber-slow.log 2>&1 & echo \$! > subscriber-slow.pid"

sleep "${DURATION_SEC}"

run_ssh "set +e; \
    cd '${BOARD_DIR}'; \
    kill \$(cat subscriber-normal.pid 2>/dev/null) 2>/dev/null; \
    kill \$(cat subscriber-slow.pid 2>/dev/null) 2>/dev/null; \
    sleep 2; \
    kill \$(cat publisher.pid 2>/dev/null) 2>/dev/null; \
    sleep 1; \
    pkill -f '[c]amera_publisher_example' 2>/dev/null; \
    pkill -f '[c]amera_subscriber_example' 2>/dev/null; \
    true"

run_scp \
    "${TARGET}:${BOARD_DIR}/publisher.log" \
    "${TARGET}:${BOARD_DIR}/subscriber-normal.log" \
    "${TARGET}:${BOARD_DIR}/subscriber-slow.log" \
    "${LOCAL_LOG_DIR}/" >/dev/null

echo "Logs copied to ${LOCAL_LOG_DIR}"
echo
echo "Publisher counters:"
grep -E "release_pending|lease_exhausted|v2_sent|release_timeout" \
    "${LOCAL_LOG_DIR}/publisher.log" | tail -n 10 || true
echo
echo "Normal subscriber summary:"
grep -E "summary|release_fail|fps=" "${LOCAL_LOG_DIR}/subscriber-normal.log" | tail -n 10 || true
echo
echo "Slow subscriber summary:"
grep -E "summary|release_fail|fps=" "${LOCAL_LOG_DIR}/subscriber-slow.log" | tail -n 10 || true

publisher_line="$(grep -E "release_pending|lease_exhausted|v2_sent|release_timeout" \
    "${LOCAL_LOG_DIR}/publisher.log" | tail -n 1 || true)"
normal_summary="$(grep -E "summary:" "${LOCAL_LOG_DIR}/subscriber-normal.log" | tail -n 1 || true)"
slow_summary="$(grep -E "summary:" "${LOCAL_LOG_DIR}/subscriber-slow.log" | tail -n 1 || true)"

echo
echo "Automatic counter checks:"

failures=0
if [[ -z "${publisher_line}" ]]; then
    echo "FAIL: publisher counter line not found"
    failures=$((failures + 1))
else
    dmabuf_enabled="$(extract_counter "${publisher_line}" "dmabuf_enabled")"
    export_fail="$(extract_counter "${publisher_line}" "export_fail")"
    v2_sent="$(extract_counter "${publisher_line}" "v2_sent")"
    v2_send_fail="$(extract_counter "${publisher_line}" "v2_send_fail")"
    release_pending="$(extract_counter "${publisher_line}" "release_pending")"
    release_timeout="$(extract_counter "${publisher_line}" "release_timeout")"

    check_eq "publisher.dmabuf_enabled" "${dmabuf_enabled:-missing}" "1" || failures=$((failures + 1))
    check_eq "publisher.export_fail" "${export_fail:-missing}" "0" || failures=$((failures + 1))
    check_ge "publisher.v2_sent" "${v2_sent:-0}" 1 || failures=$((failures + 1))
    check_le "publisher.v2_send_fail" "${v2_send_fail:-999999}" "${MAX_V2_SEND_FAIL}" || failures=$((failures + 1))
    check_eq "publisher.release_pending" "${release_pending:-missing}" "0" || failures=$((failures + 1))
    check_le "publisher.release_timeout" "${release_timeout:-999999}" "${MAX_RELEASE_TIMEOUT}" || failures=$((failures + 1))
fi

for role in normal slow; do
    if [[ "${role}" == "normal" ]]; then
        summary="${normal_summary}"
    else
        summary="${slow_summary}"
    fi

    if [[ -z "${summary}" ]]; then
        echo "FAIL: ${role} subscriber summary not found"
        failures=$((failures + 1))
        continue
    fi

    frames="$(extract_counter "${summary}" "frames")"
    save_fail="$(extract_counter "${summary}" "save_fail")"
    release_fail="$(extract_counter "${summary}" "release_fail")"
    check_ge "${role}.frames" "${frames:-0}" "${MIN_SUBSCRIBER_FRAMES}" || failures=$((failures + 1))
    check_eq "${role}.save_fail" "${save_fail:-missing}" "0" || failures=$((failures + 1))
    check_eq "${role}.release_fail" "${release_fail:-missing}" "0" || failures=$((failures + 1))
done

if (( failures > 0 )); then
    echo
    echo "rk3576_dataplane_v2_slow_consumer_result=FAIL failures=${failures}"
    exit 1
fi

echo
echo "rk3576_dataplane_v2_slow_consumer_result=PASS"
