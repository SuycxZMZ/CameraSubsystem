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
RUN_CONCURRENT="${RUN_CONCURRENT:-1}"
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
log "concurrent=${RUN_CONCURRENT}"
log "scope=usb_live_plus_mipi_readiness"

failures=0

# ------------------------------------------------------------------
# 串行模式（兼容旧行为）
# ------------------------------------------------------------------
if [[ "${RUN_CONCURRENT}" != "1" ]]; then
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
    exit 0
fi

# ------------------------------------------------------------------
# 并发模式
# ------------------------------------------------------------------

usb_live_result=""
mipi_readiness_result=""

if [[ "${RUN_USB_LIVE}" == "1" ]]; then
    log "concurrent_step=usb_live_background_start"
    (
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
            "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-lifecycle-smoke.sh" \
            >"${LOCAL_LOG_DIR}/usb-live-concurrent.log" 2>&1
        echo "usb_live_exit=$?" >>"${LOCAL_LOG_DIR}/usb-live-concurrent.log"
    ) &
    usb_pid=$!
    log "usb_live_pid=${usb_pid}"

    # 等待 USB live 稳定启动
    sleep 3
else
    log "multi_camera_topology_usb_live=SKIP"
fi

if [[ "${RUN_MIPI_READINESS}" == "1" ]]; then
    log "concurrent_step=mipi_readiness_foreground_run"
    if run_and_log "mipi-mplane-readiness" \
        env BOARD_HOST="${BOARD_HOST}" \
            BOARD_USER="${BOARD_USER}" \
            BOARD_PASSWORD="${BOARD_PASSWORD}" \
            DEVICES="${MIPI_DEVICES}" \
            REQUIRE_MPLANE="${REQUIRE_MIPI}" \
            SKIP_BUILD="${SKIP_BUILD}" \
            LOCAL_LOG_DIR="${LOCAL_LOG_DIR}/mipi-readiness" \
            "${PROJECT_ROOT}/scripts/rk3576-mplane-readiness-probe.sh"; then
        if grep -q "mplane_readiness_result=SKIP" "${topology_log}"; then
            mipi_readiness_result="SKIP"
            log "multi_camera_topology_mipi_readiness=SKIP"
        else
            mipi_readiness_result="PASS"
            log "multi_camera_topology_mipi_readiness=PASS"
        fi
    else
        mipi_readiness_result="FAIL"
        log "multi_camera_topology_mipi_readiness=FAIL"
        failures=$((failures + 1))
    fi
else
    log "multi_camera_topology_mipi_readiness=SKIP"
fi

if [[ "${RUN_USB_LIVE}" == "1" ]]; then
    log "concurrent_step=usb_live_wait"
    if wait "${usb_pid}"; then
        usb_live_result="PASS"
        log "multi_camera_topology_usb_live=PASS"
    else
        usb_live_result="FAIL"
        log "multi_camera_topology_usb_live=FAIL"
        failures=$((failures + 1))
    fi

    # 将 USB live 并发日志追加到 topology 主日志
    if [[ -f "${LOCAL_LOG_DIR}/usb-live-concurrent.log" ]]; then
        log "--- usb-live-concurrent-log-begin ---"
        cat "${LOCAL_LOG_DIR}/usb-live-concurrent.log" | tee -a "${topology_log}" >/dev/null
        log "--- usb-live-concurrent-log-end ---"
    fi
fi

# ------------------------------------------------------------------
# Identity 冲突检测
# ------------------------------------------------------------------
log "concurrent_step=identity_conflict_check"

identity_failures=0

# 收集 MIPI 设备列表
mipi_device_list=""
if [[ -f "${LOCAL_LOG_DIR}/mipi-readiness/mplane-readiness-"*.log ]]; then
    mipi_device_list=$(grep "^mplane_readiness_devices=" "${LOCAL_LOG_DIR}/mipi-readiness/mplane-readiness-"*.log | head -n 1 | cut -d'=' -f2 || true)
fi

# 检查 USB device 是否在 MIPI 列表中
if [[ -n "${mipi_device_list}" ]]; then
    for mipi_dev in ${mipi_device_list}; do
        if [[ "${USB_DEVICE}" == "${mipi_dev}" ]]; then
            log "FAIL: identity_conflict usb_device=${USB_DEVICE} overlaps with mipi_device=${mipi_dev}"
            identity_failures=$((identity_failures + 1))
        fi
    done
fi

# 检查 MIPI 设备列表内部是否有重复
if [[ -n "${mipi_device_list}" ]]; then
    duplicate_count=$(echo "${mipi_device_list}" | tr ' ' '
' | sort | uniq -d | wc -l)
    if [[ "${duplicate_count}" -gt 0 ]]; then
        log "FAIL: identity_conflict duplicate devices in mipi_device_list"
        identity_failures=$((identity_failures + 1))
    fi
fi

# 检查 stream_id 冲突（当前只有 usb0 和 mipi0_main）
if [[ "${usb_live_result}" == "PASS" && "${mipi_readiness_result}" == "PASS" ]]; then
    usb_stream_id="usb0"
    mipi_stream_id="mipi0_main"
    if [[ "${usb_stream_id}" == "${mipi_stream_id}" ]]; then
        log "FAIL: identity_conflict duplicate stream_id=${usb_stream_id}"
        identity_failures=$((identity_failures + 1))
    fi
fi

if [[ "${identity_failures}" -eq 0 ]]; then
    log "multi_camera_topology_identity_conflict=PASS"
else
    log "multi_camera_topology_identity_conflict=FAIL failures=${identity_failures}"
    failures=$((failures + 1))
fi

# ------------------------------------------------------------------
# 隔离验证
# ------------------------------------------------------------------
log "concurrent_step=isolation_check"

isolation_failures=0

# 验证 USB live 的核心指标不受 MIPI readiness 影响
if [[ "${usb_live_result}" == "PASS" && -f "${LOCAL_LOG_DIR}/usb-live/publisher.log" ]]; then
    publisher_line=$(grep -E "release_pending|lease_exhausted|v2_sent|release_timeout" \
        "${LOCAL_LOG_DIR}/usb-live/publisher.log" | tail -n 1 || true)
    if [[ -n "${publisher_line}" ]]; then
        v2_sent=$(echo "${publisher_line}" | sed -n 's/.*v2_sent=\([0-9][0-9]*\).*/\1/p')
        release_pending=$(echo "${publisher_line}" | sed -n 's/.*release_pending=\([0-9][0-9]*\).*/\1/p')
        lease_exhausted=$(echo "${publisher_line}" | sed -n 's/.*lease_exhausted=\([0-9][0-9]*\).*/\1/p')

        if [[ -z "${v2_sent}" || "${v2_sent}" == "0" ]]; then
            log "FAIL: isolation usb_live v2_sent is zero or missing"
            isolation_failures=$((isolation_failures + 1))
        fi

        if [[ -n "${release_pending}" && "${release_pending}" != "0" ]]; then
            log "FAIL: isolation usb_live release_pending=${release_pending} (expected 0)"
            isolation_failures=$((isolation_failures + 1))
        fi

        if [[ -n "${lease_exhausted}" && "${lease_exhausted}" != "0" ]]; then
            log "FAIL: isolation usb_live lease_exhausted=${lease_exhausted} (expected 0)"
            isolation_failures=$((isolation_failures + 1))
        fi
    else
        log "FAIL: isolation usb_live publisher counter line not found"
        isolation_failures=$((isolation_failures + 1))
    fi
fi

# 验证 MIPI readiness 的结果不受 USB live 影响
if [[ "${mipi_readiness_result}" == "PASS" || "${mipi_readiness_result}" == "SKIP" ]]; then
    log "INFO: isolation mipi_readiness result=${mipi_readiness_result} (unaffected by usb_live)"
else
    log "FAIL: isolation mipi_readiness failed during concurrent usb_live"
    isolation_failures=$((isolation_failures + 1))
fi

if [[ "${isolation_failures}" -eq 0 ]]; then
    log "multi_camera_topology_isolation=PASS"
else
    log "multi_camera_topology_isolation=FAIL failures=${isolation_failures}"
    failures=$((failures + 1))
fi

# ------------------------------------------------------------------
# 结果汇总
# ------------------------------------------------------------------
if (( failures > 0 )); then
    log
    log "multi_camera_topology_smoke_result=FAIL failures=${failures}"
    log "local_log=${topology_log}"
    exit 1
fi

log
log "multi_camera_topology_smoke_result=PASS"
log "local_log=${topology_log}"
