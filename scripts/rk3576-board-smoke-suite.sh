#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
DEVICE="${DEVICE:-/dev/video45}"
SUITES="${SUITES:-dataplane-lifecycle codec-mp4 web-record-mp4 web-codec-restart-mp4}"
SKIP_BUILD="${SKIP_BUILD:-1}"
DEPLOY_FIRST="${DEPLOY_FIRST:-0}"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

usage()
{
    cat <<EOF
Usage: $0 [suite ...]

Default suites:
  ${SUITES}

Available suites:
  dataplane-lifecycle
  dataplane-failover
  dataplane-release-disconnect
  codec-v1
  codec-mp4
  codec-stability
  web-record-raw
  web-record-mp4
  web-codec-restart-raw
  web-codec-restart-mp4

Environment:
  BOARD_HOST=${BOARD_HOST}
  BOARD_USER=${BOARD_USER}
  REMOTE_ROOT=${REMOTE_ROOT}
  DEPLOY_FIRST=${DEPLOY_FIRST}
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

run_suite()
{
    local name="$1"
    echo
    echo "============================================================"
    echo "RK3576 smoke suite: ${name}"
    echo "============================================================"
    case "${name}" in
        dataplane-lifecycle)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICE="${DEVICE}" SKIP_BUILD="${SKIP_BUILD}" DURATION_SEC="${DURATION_SEC:-60}" \
            SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC:-5}" SUBSCRIBER_COUNT="${SUBSCRIBER_COUNT:-2}" \
                "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-lifecycle-smoke.sh"
            ;;
        dataplane-failover)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICE="${DEVICE}" SKIP_BUILD="${SKIP_BUILD}" PRE_CRASH_SEC="${PRE_CRASH_SEC:-6}" \
            POST_CRASH_SEC="${POST_CRASH_SEC:-8}" \
                "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-failover-smoke.sh"
            ;;
        dataplane-release-disconnect)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICE="${DEVICE}" SKIP_BUILD="${SKIP_BUILD}" FAULT_MODE=release-disconnect \
            FAULT_RELEASE_DISCONNECT_AFTER_FRAMES="${FAULT_RELEASE_DISCONNECT_AFTER_FRAMES:-5}" \
            CRASH_RELEASE_DELAY_MS=0 PRE_CRASH_SEC="${PRE_CRASH_SEC:-6}" POST_CRASH_SEC="${POST_CRASH_SEC:-8}" \
                "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-failover-smoke.sh"
            ;;
        codec-v1)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' sh scripts/codec-v1-smoke-rk3576.sh"
            ;;
        codec-mp4)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' DURATION_SEC='${CODEC_MP4_DURATION_SEC:-5}' sh scripts/codec-mp4-smoke-rk3576.sh"
            ;;
        codec-stability)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' sh scripts/codec-stability-test-rk3576.sh"
            ;;
        web-record-raw)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' RECORD_CONTAINER=raw_h264 RECORD_CYCLES='${WEB_RECORD_CYCLES:-1}' sh scripts/web-record-freeze-smoke-rk3576.sh"
            ;;
        web-record-mp4)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' RECORD_CONTAINER=mp4 RECORD_CYCLES='${WEB_RECORD_CYCLES:-1}' sh scripts/web-record-freeze-smoke-rk3576.sh"
            ;;
        web-codec-restart-raw)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' RECORD_CONTAINER=raw_h264 sh scripts/web-codec-restart-smoke-rk3576.sh"
            ;;
        web-codec-restart-mp4)
            run_ssh "cd '${REMOTE_ROOT}' && DEVICE='${DEVICE}' RECORD_CONTAINER=mp4 sh scripts/web-codec-restart-smoke-rk3576.sh"
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            echo "Unknown suite: ${name}" >&2
            usage
            return 2
            ;;
    esac
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "${DEPLOY_FIRST}" == "1" ]]; then
    BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" REMOTE_ROOT="${REMOTE_ROOT}" \
        "${PROJECT_ROOT}/scripts/deploy-rk3576-web-debug.sh"
fi

if (( $# > 0 )); then
    selected_suites="$*"
else
    selected_suites="${SUITES}"
fi

failed=0
for suite in ${selected_suites}; do
    if ! run_suite "${suite}"; then
        echo "SUITE_RESULT ${suite}=FAIL"
        failed=$((failed + 1))
    else
        echo "SUITE_RESULT ${suite}=PASS"
    fi
done

if (( failed > 0 )); then
    echo
    echo "rk3576_board_smoke_suite_result=FAIL failures=${failed}"
    exit 1
fi

echo
echo "rk3576_board_smoke_suite_result=PASS"
