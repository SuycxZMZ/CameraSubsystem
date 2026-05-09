#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
USB_DEVICE="${USB_DEVICE:-${DEVICE:-/dev/video45}}"
MIPI_DEVICES="${MIPI_DEVICES:-}"
REQUIRE_MIPI="${REQUIRE_MIPI:-0}"
SKIP_BUILD="${SKIP_BUILD:-1}"
USB_DURATION_SEC="${USB_DURATION_SEC:-20}"
USB_SUBSCRIBER_COUNT="${USB_SUBSCRIBER_COUNT:-1}"
SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC:-5}"
RUN_USB_LIVE="${RUN_USB_LIVE:-1}"
RUN_MIPI_READINESS="${RUN_MIPI_READINESS:-1}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-multi-camera-topology-smoke}"

mkdir -p "${LOCAL_LOG_DIR}"

topology_log="${LOCAL_LOG_DIR}/topology-$(date +%Y%m%d-%H%M%S).log"

log()
{
    echo "$@" | tee -a "${topology_log}"
}

run_and_log()
{
    local name="$1"
    shift
    log
    log "============================================================"
    log "multi_camera_topology_step=${name}"
    log "============================================================"
    "$@" 2>&1 | tee -a "${topology_log}"
}

log "multi_camera_topology_smoke_start=1"
log "board=${BOARD_USER}@${BOARD_HOST}"
log "usb_stream=usb0 device=${USB_DEVICE} live=${RUN_USB_LIVE}"
log "mipi_stream=mipi0_main devices=${MIPI_DEVICES:-auto} readiness=${RUN_MIPI_READINESS} require_mipi=${REQUIRE_MIPI}"
log "scope=usb_live_plus_mipi_readiness"

failures=0

if [[ "${RUN_USB_LIVE}" == "1" ]]; then
    if ! run_and_log "usb-live-dataplane-v2" \
        env BOARD_HOST="${BOARD_HOST}" \
            BOARD_USER="${BOARD_USER}" \
            BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICE="${USB_DEVICE}" \
            DURATION_SEC="${USB_DURATION_SEC}" \
            SAMPLE_INTERVAL_SEC="${SAMPLE_INTERVAL_SEC}" \
            SUBSCRIBER_COUNT="${USB_SUBSCRIBER_COUNT}" \
            SKIP_BUILD="${SKIP_BUILD}" \
            CHECK_CLEANUP=0 \
            LOCAL_LOG_DIR="${LOCAL_LOG_DIR}/usb-live" \
            "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-lifecycle-smoke.sh"; then
        log "multi_camera_topology_usb_live=FAIL"
        failures=$((failures + 1))
    else
        log "multi_camera_topology_usb_live=PASS"
    fi
else
    log "multi_camera_topology_usb_live=SKIP"
fi

if [[ "${RUN_MIPI_READINESS}" == "1" ]]; then
    if ! run_and_log "mipi-mplane-readiness" \
        env BOARD_HOST="${BOARD_HOST}" \
            BOARD_USER="${BOARD_USER}" \
            BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICES="${MIPI_DEVICES}" \
            REQUIRE_MPLANE="${REQUIRE_MIPI}" \
            SKIP_BUILD="${SKIP_BUILD}" \
            LOCAL_LOG_DIR="${LOCAL_LOG_DIR}/mipi-readiness" \
            "${PROJECT_ROOT}/scripts/rk3576-mplane-readiness-probe.sh"; then
        log "multi_camera_topology_mipi_readiness=FAIL"
        failures=$((failures + 1))
    elif grep -q "mplane_readiness_result=SKIP" "${topology_log}"; then
        log "multi_camera_topology_mipi_readiness=SKIP"
    else
        log "multi_camera_topology_mipi_readiness=PASS"
    fi
else
    log "multi_camera_topology_mipi_readiness=SKIP"
fi

if (( failures > 0 )); then
    log
    log "multi_camera_topology_smoke_result=FAIL failures=${failures}"
    log "local_log=${topology_log}"
    exit 1
fi

log
log "multi_camera_topology_smoke_result=PASS"
log "local_log=${topology_log}"
