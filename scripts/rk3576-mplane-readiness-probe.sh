#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${CAMERA_SUBSYSTEM_RK3576_OUTPUT_DIR:-${PROJECT_ROOT}/bin/rk3576}"
PROBE_BIN="${OUTPUT_DIR}/mplane_dmabuf_probe"
SOURCE_PROBE_BIN="${OUTPUT_DIR}/dmabuf_smoke_test"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
BOARD_DIR="${BOARD_DIR:-/tmp/camera_subsystem_mplane_probe}"
DEVICES="${DEVICES:-}"
WIDTH="${WIDTH:-800}"
HEIGHT="${HEIGHT:-600}"
FOURCC="${FOURCC:-NV12}"
SKIP_BUILD="${SKIP_BUILD:-1}"
REQUIRE_MPLANE="${REQUIRE_MPLANE:-0}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-mplane-readiness-probe}"

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

if [[ "${SKIP_BUILD}" != "1" ]]; then
    "${PROJECT_ROOT}/scripts/build-rk3576.sh"
fi

if [[ ! -x "${PROBE_BIN}" ]]; then
    echo "missing mplane probe binary: ${PROBE_BIN}"
    echo "run ./scripts/build-rk3576.sh first, or set SKIP_BUILD=0"
    exit 1
fi

if [[ ! -x "${SOURCE_PROBE_BIN}" ]]; then
    echo "missing CameraSource probe binary: ${SOURCE_PROBE_BIN}"
    echo "run ./scripts/build-rk3576.sh first, or set SKIP_BUILD=0"
    exit 1
fi

mkdir -p "${LOCAL_LOG_DIR}"
run_ssh "mkdir -p '${BOARD_DIR}'"
run_scp "${PROBE_BIN}" "${TARGET}:${BOARD_DIR}/mplane_dmabuf_probe"
run_scp "${SOURCE_PROBE_BIN}" "${TARGET}:${BOARD_DIR}/dmabuf_smoke_test"
run_ssh "chmod +x '${BOARD_DIR}/mplane_dmabuf_probe' '${BOARD_DIR}/dmabuf_smoke_test'"

remote_runner="$(mktemp)"
trap 'rm -f "${remote_runner}"' EXIT

cat >"${remote_runner}" <<'REMOTE_RUNNER'
set -eu
cd "$1"
devices="$2"
width="$3"
height="$4"
fourcc="$5"
require_mplane="$6"

if [ -z "${devices}" ]; then
    for node in /sys/class/video4linux/video*; do
        [ -e "${node}/name" ] || continue
        name="$(cat "${node}/name" 2>/dev/null || true)"
        case "${name}" in
            rkisp_mainpath|rkisp_selfpath|rkisp_ldcpath|rkvpss_scale*)
                devices="${devices} /dev/$(basename "${node}")"
                ;;
        esac
    done
fi

if [ -z "${devices}" ]; then
    devices="$(ls /dev/video* 2>/dev/null | tr "\n" " " || true)"
fi

echo "mplane_readiness_devices=${devices:-none}"
if [ -z "${devices}" ]; then
    echo "mplane_readiness_result=SKIP reason=no_video_nodes"
    [ "${require_mplane}" = "1" ] && exit 1
    exit 0
fi

pass_count=0
source_pass_count=0
mplane_count=0
fail_count=0
skip_count=0

for dev in ${devices}; do
    if [ ! -c "${dev}" ]; then
        echo "device=${dev} result=SKIP reason=not_char_device"
        skip_count=$((skip_count + 1))
        continue
    fi

    log_name="$(basename "${dev}").log"
    set +e
    ./mplane_dmabuf_probe "${dev}" "${width}" "${height}" "${fourcc}" >"${log_name}" 2>&1
    rc=$?
    set -e

    cat "${log_name}"
    if grep -q "supports_mplane=1" "${log_name}"; then
        mplane_count=$((mplane_count + 1))
    fi

    if [ "${rc}" -eq 0 ]; then
        echo "device=${dev} result=PASS"
        pass_count=$((pass_count + 1))

        source_log_name="$(basename "${dev}").camera_source_probe.log"
        set +e
        CAMERA_SUBSYSTEM_ENABLE_MPLANE_PROBE=1 \
        CAMERA_SUBSYSTEM_MPLANE_PROBE_WIDTH="${width}" \
        CAMERA_SUBSYSTEM_MPLANE_PROBE_HEIGHT="${height}" \
        CAMERA_SUBSYSTEM_MPLANE_PROBE_FOURCC="${fourcc}" \
            ./dmabuf_smoke_test "${dev}" 0 >"${source_log_name}" 2>&1
        source_rc=$?
        set -e
        cat "${source_log_name}"
        if [ "${source_rc}" -eq 0 ] && grep -q "mplane_probe_only_result=PASS" "${source_log_name}"; then
            echo "device=${dev} camera_source_mplane_probe=PASS"
            source_pass_count=$((source_pass_count + 1))
        else
            echo "device=${dev} camera_source_mplane_probe=FAIL rc=${source_rc}"
            fail_count=$((fail_count + 1))
        fi
    elif grep -q "supports_mplane=0" "${log_name}"; then
        echo "device=${dev} result=SKIP reason=no_mplane_capability"
        skip_count=$((skip_count + 1))
    elif grep -q "errno=19 msg=No such device" "${log_name}"; then
        echo "device=${dev} result=SKIP reason=no_bound_device"
        skip_count=$((skip_count + 1))
    elif grep -q "VIDIOC_QUERYCAP failed" "${log_name}"; then
        echo "device=${dev} result=SKIP reason=not_v4l2_capture_node"
        skip_count=$((skip_count + 1))
    elif grep -Eq "card=.*(iqtool|statistics|input-params)" "${log_name}"; then
        echo "device=${dev} result=SKIP reason=metadata_node"
        skip_count=$((skip_count + 1))
    else
        echo "device=${dev} result=FAIL rc=${rc}"
        fail_count=$((fail_count + 1))
    fi
done

echo "mplane_readiness_summary pass=${pass_count} camera_source_probe_pass=${source_pass_count} mplane_candidates=${mplane_count} fail=${fail_count} skip=${skip_count}"
if [ "${pass_count}" -gt 0 ] && [ "${source_pass_count}" -eq "${pass_count}" ] && [ "${fail_count}" -eq 0 ]; then
    echo "mplane_readiness_result=PASS"
    exit 0
fi

if [ "${mplane_count}" -eq 0 ] && [ "${require_mplane}" != "1" ]; then
    echo "mplane_readiness_result=SKIP reason=no_mplane_nodes"
    exit 0
fi

echo "mplane_readiness_result=FAIL"
exit 1
REMOTE_RUNNER

run_scp "${remote_runner}" "${TARGET}:${BOARD_DIR}/run-mplane-readiness.sh"
run_ssh "chmod +x '${BOARD_DIR}/run-mplane-readiness.sh'"

log_file="${LOCAL_LOG_DIR}/mplane-readiness-$(date +%Y%m%d-%H%M%S).log"
run_ssh "sh '${BOARD_DIR}/run-mplane-readiness.sh' '${BOARD_DIR}' '${DEVICES}' '${WIDTH}' '${HEIGHT}' '${FOURCC}' '${REQUIRE_MPLANE}'" \
    | tee "${log_file}"

echo "local_log=${log_file}"
