#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
DEVICE="${DEVICE:-/dev/video45}"
# --- Tier definitions ---
readonly TIER_QUICK="codec-mp4 web-record-mp4 web-codec-restart-mp4"
readonly TIER_FULL="dataplane-lifecycle codec-multi-session-control codec-mp4 web-record-mp4 web-codec-restart-mp4"
readonly TIER_EXTENDED="dataplane-lifecycle dataplane-failover dataplane-release-disconnect codec-v1 codec-multi-session-control codec-mp4 codec-stability web-record-raw web-record-mp4 web-codec-restart-raw web-codec-restart-mp4 multi-camera-topology"

TIER="${TIER:-}"
SUITES="${SUITES:-}"
SKIP_BUILD="${SKIP_BUILD:-1}"
DEPLOY_FIRST="${DEPLOY_FIRST:-0}"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

resolve_tier()
{
    case "${TIER}" in
        quick)    echo "${TIER_QUICK}" ;;
        full)     echo "${TIER_FULL}" ;;
        extended) echo "${TIER_EXTENDED}" ;;
        "")       echo "${TIER_FULL}" ;;
        *)        echo "Unknown TIER: '${TIER}' (expected: quick|full|extended)" >&2; return 1 ;;
    esac
}

usage()
{
    cat <<EOF
Usage: $0 [suite ...]

Tiers (select via TIER environment variable):
  quick     (~60s)  codec-mp4 web-record-mp4 web-codec-restart-mp4
  full      (~135s) dataplane-lifecycle codec-multi-session-control codec-mp4 web-record-mp4 web-codec-restart-mp4  [default]
  extended  (~5-8m) all suites, including multi-camera topology smoke

Available suites:
  stream-metrics
  dataplane-lifecycle
  dataplane-failover
  dataplane-release-disconnect
  codec-v1
  codec-multi-session-control
  codec-mp4
  codec-stability
  web-record-raw
  web-record-mp4
  web-codec-restart-raw
  web-codec-restart-mp4
  mplane-readiness
  multi-camera-topology

Environment:
  TIER=${TIER:-<not set, defaults to full>}
  SUITES=${SUITES:-<not set>}
  BOARD_HOST=${BOARD_HOST}
  BOARD_USER=${BOARD_USER}
  REMOTE_ROOT=${REMOTE_ROOT}
  DEPLOY_FIRST=${DEPLOY_FIRST}

Priority: CLI args > SUITES env > TIER env > default (full)
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
        stream-metrics)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICE="${DEVICE}" SKIP_BUILD="${SKIP_BUILD}" DURATION_SEC="${DURATION_SEC:-60}" \
            SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC:-5}" SUBSCRIBER_COUNT="${SUBSCRIBER_COUNT:-2}" \
                "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-lifecycle-smoke.sh"
            ;;
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
        codec-multi-session-control)
            run_ssh "cd '${REMOTE_ROOT}' && sh scripts/codec-multi-session-control-smoke-rk3576.sh"
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
        mplane-readiness)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            SKIP_BUILD="${SKIP_BUILD}" \
                "${PROJECT_ROOT}/scripts/rk3576-mplane-readiness-probe.sh"
            ;;
        multi-camera-topology)
            BOARD_HOST="${BOARD_HOST}" BOARD_USER="${BOARD_USER}" BOARD_PASSWORD="${BOARD_PASSWORD}" \
            USB_DEVICE="${USB_DEVICE:-${DEVICE}}" MIPI_DEVICES="${MIPI_DEVICES:-}" \
            REQUIRE_MIPI="${REQUIRE_MIPI:-0}" SKIP_BUILD="${SKIP_BUILD}" \
                "${PROJECT_ROOT}/scripts/rk3576-multi-camera-topology-smoke.sh"
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
    active_tier="cli"
elif [[ -n "${SUITES}" ]]; then
    selected_suites="${SUITES}"
    active_tier="SUITES-override"
else
    selected_suites="$(resolve_tier)" || exit 1
    active_tier="${TIER:-full}"
fi

failed=0
echo "RK3576 smoke tier=${active_tier} suites=${selected_suites}"
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
