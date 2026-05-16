#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${CAMERA_SUBSYSTEM_RK3576_OUTPUT_DIR:-${PROJECT_ROOT}/bin/rk3576}"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
BOARD_DIR="${BOARD_DIR:-/tmp/camera_subsystem_dmabuf_v2_lifecycle}"
DEVICE="${DEVICE:-/dev/video45}"
DURATION_SEC="${DURATION_SEC:-90}"
SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC:-5}"
SUBSCRIBER_COUNT="${SUBSCRIBER_COUNT:-2}"
PROCESS_DELAY_MS="${PROCESS_DELAY_MS:-5}"
RELEASE_DELAY_MS="${RELEASE_DELAY_MS:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
MIN_SUBSCRIBER_FRAMES="${MIN_SUBSCRIBER_FRAMES:-20}"
MAX_SUBSCRIBER_RELEASE_FAIL="${MAX_SUBSCRIBER_RELEASE_FAIL:-2}"
MAX_PUBLISHER_FD_DRIFT="${MAX_PUBLISHER_FD_DRIFT:-4}"
MAX_SUBSCRIBER_FD_DRIFT="${MAX_SUBSCRIBER_FD_DRIFT:-4}"
MAX_RELEASE_PENDING="${MAX_RELEASE_PENDING:-0}"
MAX_ACTIVE_LEASES="${MAX_ACTIVE_LEASES:-0}"
MAX_RELEASE_TIMEOUT="${MAX_RELEASE_TIMEOUT:-0}"
MAX_LEASE_EXHAUSTED="${MAX_LEASE_EXHAUSTED:-0}"
MAX_EXPORT_FAIL="${MAX_EXPORT_FAIL:-0}"
CHECK_CLEANUP="${CHECK_CLEANUP:-1}"
REQUESTED_FPS="${REQUESTED_FPS:-15}"
DEVICE_DISCOVERY_STATUS="${DEVICE_DISCOVERY_STATUS:-0}"

CONTROL_SOCKET="/tmp/camera_subsystem_control.sock"
DATA_SOCKET="/tmp/camera_subsystem_data_v2.sock"
RELEASE_SOCKET="/tmp/camera_subsystem_release_v2.sock"

LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-dataplane-v2-lifecycle-smoke}"
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

fd_count_range()
{
    local label="$1"
    local file="$2"
    awk -v target="${label}" '
        $2 == target && $3 ~ /^[0-9]+$/ {
            if (!seen || $3 < min) min = $3
            if (!seen || $3 > max) max = $3
            seen = 1
        }
        END {
            if (seen) {
                printf "%d %d %d\n", min, max, max - min
            }
        }
    ' "${file}"
}

if [[ "${SKIP_BUILD}" != "1" ]]; then
    "${PROJECT_ROOT}/scripts/build-rk3576.sh"
fi

mkdir -p "${LOCAL_LOG_DIR}"

run_ssh "mkdir -p '${BOARD_DIR}'"
run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    pkill -f '[c]amera_publisher_example' 2>/dev/null || true; \
    pkill -f '[c]amera_subscriber_example' 2>/dev/null || true; \
    for _ in 1 2 3 4 5; do \
        if ! pgrep -f '[c]amera_publisher_example|[c]amera_subscriber_example' >/dev/null 2>&1; then \
            break; \
        fi; \
        sleep 1; \
    done; \
    pkill -9 -f '[c]amera_publisher_example' 2>/dev/null || true; \
    pkill -9 -f '[c]amera_subscriber_example' 2>/dev/null || true; \
    rm -f '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${RELEASE_SOCKET}'; \
    rm -f publisher.log fd_samples.log lifecycle_status.log subscriber-*.log *.pid; \
    rm -rf frames-*; \
    mkdir -p frames"
run_scp \
    "${OUTPUT_DIR}/camera_publisher_example" \
    "${TARGET}:${BOARD_DIR}/camera_publisher_example.upload" >/dev/null
run_scp \
    "${OUTPUT_DIR}/camera_subscriber_example" \
    "${TARGET}:${BOARD_DIR}/camera_subscriber_example.upload" >/dev/null
run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    mv camera_publisher_example.upload camera_publisher_example; \
    mv camera_subscriber_example.upload camera_subscriber_example; \
    chmod +x camera_publisher_example camera_subscriber_example"

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    device_discovery_args=''; \
    if [ '${DEVICE_DISCOVERY_STATUS}' = '1' ]; then \
        device_discovery_args='--device-discovery-status-path ${BOARD_DIR}/device_discovery_status.json'; \
    fi; \
    nohup ./camera_publisher_example '${DEVICE}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' \
        --io-method dmabuf --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
        --metrics-history-path '${BOARD_DIR}/metrics_history.jsonl' \
        --metrics-history-interval '${SAMPLE_INTERVAL_SEC}' \
        --metrics-snapshot-path '${BOARD_DIR}/metrics_snapshot.json' \
        \${device_discovery_args} \
        > publisher.log 2>&1 & echo \$! > publisher.pid"

sleep 2

for index in $(seq 1 "${SUBSCRIBER_COUNT}"); do
    run_ssh "set -e; \
        cd '${BOARD_DIR}'; \
        mkdir -p 'frames-${index}'; \
        nohup ./camera_subscriber_example 'frames-${index}' '${CONTROL_SOCKET}' '${DATA_SOCKET}' '${DEVICE}' \
            --data-plane v2 --release-socket '${RELEASE_SOCKET}' \
            --process-delay-ms '${PROCESS_DELAY_MS}' --release-delay-ms '${RELEASE_DELAY_MS}' \
            > 'subscriber-${index}.log' 2>&1 & echo \$! > 'subscriber-${index}.pid'"
done

run_ssh "set -e; \
    cd '${BOARD_DIR}'; \
    : > fd_samples.log; \
    end=\$((\$(date +%s) + ${DURATION_SEC})); \
    while [ \$(date +%s) -lt \${end} ]; do \
        now=\$(date +%s); \
        ppid=\$(cat publisher.pid 2>/dev/null || true); \
        if [ -n \"\${ppid}\" ] && [ -d \"/proc/\${ppid}/fd\" ]; then \
            echo \"\${now} publisher \$(ls -1 /proc/\${ppid}/fd 2>/dev/null | wc -l)\" >> fd_samples.log; \
        fi; \
        for pidfile in subscriber-*.pid; do \
            [ -f \"\${pidfile}\" ] || continue; \
            role=\${pidfile%.pid}; \
            spid=\$(cat \"\${pidfile}\" 2>/dev/null || true); \
            if [ -n \"\${spid}\" ] && [ -d \"/proc/\${spid}/fd\" ]; then \
                echo \"\${now} \${role} \$(ls -1 /proc/\${spid}/fd 2>/dev/null | wc -l)\" >> fd_samples.log; \
            fi; \
        done; \
        sleep '${SAMPLE_INTERVAL_SEC}'; \
    done"

run_ssh "set +e; \
    cd '${BOARD_DIR}'; \
    kill \$(cat publisher.pid 2>/dev/null) 2>/dev/null; \
    sleep 5; \
    remaining_publishers=\$(pgrep -f '[c]amera_publisher_example' | wc -l); \
    remaining_subscribers=\$(pgrep -f '[c]amera_subscriber_example' | wc -l); \
    sockets_left=0; \
    [ -e '${CONTROL_SOCKET}' ] && sockets_left=\$((sockets_left + 1)); \
    [ -e '${DATA_SOCKET}' ] && sockets_left=\$((sockets_left + 1)); \
    [ -e '${RELEASE_SOCKET}' ] && sockets_left=\$((sockets_left + 1)); \
    echo \"remaining_publishers=\${remaining_publishers}\" > lifecycle_status.log; \
    echo \"remaining_subscribers=\${remaining_subscribers}\" >> lifecycle_status.log; \
    echo \"sockets_left=\${sockets_left}\" >> lifecycle_status.log; \
    pkill -f '[c]amera_publisher_example' 2>/dev/null; \
    pkill -f '[c]amera_subscriber_example' 2>/dev/null; \
    sleep 1; \
    pkill -9 -f '[c]amera_publisher_example' 2>/dev/null; \
    pkill -9 -f '[c]amera_subscriber_example' 2>/dev/null; \
    true"

# 拉取必需日志：失败必须暴露
run_scp \
    "${TARGET}:${BOARD_DIR}/publisher.log" \
    "${TARGET}:${BOARD_DIR}/fd_samples.log" \
    "${TARGET}:${BOARD_DIR}/lifecycle_status.log" \
    "${TARGET}:${BOARD_DIR}/subscriber-*.log" \
    "${LOCAL_LOG_DIR}/" >/dev/null

# 拉取可选 metrics 文件：失败不阻断
run_scp \
    "${TARGET}:${BOARD_DIR}/metrics_history.jsonl" \
    "${TARGET}:${BOARD_DIR}/metrics_snapshot.json" \
    "${TARGET}:${BOARD_DIR}/device_discovery_status.json" \
    "${LOCAL_LOG_DIR}/" >/dev/null || true

echo "Logs copied to ${LOCAL_LOG_DIR}"
echo
echo "FD samples:"
tail -n 20 "${LOCAL_LOG_DIR}/fd_samples.log" || true
echo
echo "Lifecycle status:"
cat "${LOCAL_LOG_DIR}/lifecycle_status.log" || true
echo
echo "Subscriber summaries:"
grep -E "summary|release_fail|fps=" "${LOCAL_LOG_DIR}"/subscriber-*.log | tail -n 20 || true

echo
echo "Automatic lifecycle checks:"

failures=0

# === Metrics-based publisher checks (replaces grep-based counter extraction) ===
metrics_history_local="${LOCAL_LOG_DIR}/metrics_history.jsonl"
metrics_snapshot_local="${LOCAL_LOG_DIR}/metrics_snapshot.json"

# Build Python evaluator args — always pass history path (Python handles missing/empty);
# pass snapshot as fallback if available.
py_args=("${PROJECT_ROOT}/scripts/metrics_smoke_evaluator.py")
py_args+=(--history "${metrics_history_local}")
if [[ -s "${metrics_snapshot_local}" ]]; then
    py_args+=(--snapshot "${metrics_snapshot_local}")
fi

py_args+=(
    --tier "${TIER:-full}"
    --output-report "${LOCAL_LOG_DIR}/metrics_smoke_report.json"
    --device "${DEVICE}"
    --duration-sec "${DURATION_SEC}"
    --subscriber-count "${SUBSCRIBER_COUNT}"
    --requested-fps "${REQUESTED_FPS}"
)

metrics_failures=0
set +e
python3 "${py_args[@]}"
metrics_exit=$?
set -e
if (( metrics_exit == 0 )); then
    echo "PASS: metrics evaluator"
elif (( metrics_exit == 1 )); then
    echo "FAIL: metrics evaluator detected threshold breach"
    metrics_failures=$((metrics_failures + 1))
elif (( metrics_exit == 2 )); then
    echo "ERROR: metrics evaluator encountered input/environment error (exit 2)"
    metrics_failures=$((metrics_failures + 1))
else
    echo "ERROR: metrics evaluator unexpected exit code ${metrics_exit}"
    metrics_failures=$((metrics_failures + 1))
fi
failures=$((failures + metrics_failures))

if [[ "${DEVICE_DISCOVERY_STATUS}" == "1" ]]; then
    discovery_status_local="${LOCAL_LOG_DIR}/device_discovery_status.json"
    if [[ -s "${discovery_status_local}" ]]; then
        echo "PASS: device_discovery_status.exists"
    else
        echo "FAIL: device_discovery_status.exists"
        failures=$((failures + 1))
    fi

    if grep -q '"physical_id":"usb:' "${discovery_status_local}" 2>/dev/null; then
        echo "PASS: device_discovery_status.physical_id"
    else
        echo "FAIL: device_discovery_status.physical_id"
        failures=$((failures + 1))
    fi

    if grep -q 'device discovery snapshot' "${LOCAL_LOG_DIR}/publisher.log" 2>/dev/null; then
        echo "PASS: publisher.device_discovery_snapshot"
    else
        echo "FAIL: publisher.device_discovery_snapshot"
        failures=$((failures + 1))
    fi
fi

publisher_range="$(fd_count_range publisher "${LOCAL_LOG_DIR}/fd_samples.log")"
if [[ -z "${publisher_range}" ]]; then
    echo "FAIL: publisher fd samples not found"
    failures=$((failures + 1))
else
    read -r publisher_fd_min publisher_fd_max publisher_fd_drift <<<"${publisher_range}"
    echo "INFO: publisher.fd_min=${publisher_fd_min} fd_max=${publisher_fd_max}"
    check_le "publisher.fd_drift" "${publisher_fd_drift}" "${MAX_PUBLISHER_FD_DRIFT}" || failures=$((failures + 1))
fi

for index in $(seq 1 "${SUBSCRIBER_COUNT}"); do
    label="subscriber-${index}"
    summary="$(grep -E "summary:" "${LOCAL_LOG_DIR}/${label}.log" | tail -n 1 || true)"
    if [[ -z "${summary}" ]]; then
        echo "FAIL: ${label} summary not found"
        failures=$((failures + 1))
    else
        frames="$(extract_counter "${summary}" "frames")"
        save_fail="$(extract_counter "${summary}" "save_fail")"
        release_fail="$(extract_counter "${summary}" "release_fail")"
        check_ge "${label}.frames" "${frames:-0}" "${MIN_SUBSCRIBER_FRAMES}" || failures=$((failures + 1))
        check_eq "${label}.save_fail" "${save_fail:-missing}" "0" || failures=$((failures + 1))
        check_le "${label}.release_fail" "${release_fail:-999999}" "${MAX_SUBSCRIBER_RELEASE_FAIL}" || failures=$((failures + 1))
    fi

    subscriber_range="$(fd_count_range "${label}" "${LOCAL_LOG_DIR}/fd_samples.log")"
    if [[ -z "${subscriber_range}" ]]; then
        echo "FAIL: ${label} fd samples not found"
        failures=$((failures + 1))
    else
        read -r subscriber_fd_min subscriber_fd_max subscriber_fd_drift <<<"${subscriber_range}"
        echo "INFO: ${label}.fd_min=${subscriber_fd_min} fd_max=${subscriber_fd_max}"
        check_le "${label}.fd_drift" "${subscriber_fd_drift}" "${MAX_SUBSCRIBER_FD_DRIFT}" || failures=$((failures + 1))
    fi
done

remaining_publishers="$(extract_counter "$(cat "${LOCAL_LOG_DIR}/lifecycle_status.log")" "remaining_publishers")"
remaining_subscribers="$(extract_counter "$(cat "${LOCAL_LOG_DIR}/lifecycle_status.log")" "remaining_subscribers")"
sockets_left="$(extract_counter "$(cat "${LOCAL_LOG_DIR}/lifecycle_status.log")" "sockets_left")"
if [[ "${CHECK_CLEANUP}" == "1" ]]; then
    check_eq "cleanup.remaining_publishers" "${remaining_publishers:-missing}" "0" || failures=$((failures + 1))
    check_eq "cleanup.remaining_subscribers" "${remaining_subscribers:-missing}" "0" || failures=$((failures + 1))
    check_eq "cleanup.sockets_left" "${sockets_left:-missing}" "0" || failures=$((failures + 1))
else
    echo "INFO: cleanup check skipped remaining_publishers=${remaining_publishers:-missing} remaining_subscribers=${remaining_subscribers:-missing} sockets_left=${sockets_left:-missing}"
fi

if (( failures > 0 )); then
    echo
    echo "rk3576_dataplane_v2_lifecycle_result=FAIL failures=${failures}"
    exit 1
fi

echo
echo "rk3576_dataplane_v2_lifecycle_result=PASS"
