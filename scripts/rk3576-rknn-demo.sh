#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${OMNI3576_SDK_ROOT:-${PROJECT_ROOT}/../Omni3576-sdk}"
DEMO_ROOT="${RKNN_DEMO_ROOT:-${SDK_ROOT}/external/rknpu2/examples/rknn_yolov5_demo}"
TOOLCHAIN_PREFIX="${RKNN_GCC_COMPILER:-${SDK_ROOT}/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu}"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem/rknn_demo}"

TARGET_SOC="${TARGET_SOC:-rk3576}"
TARGET_ARCH="${TARGET_ARCH:-aarch64}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
LOCAL_INSTALL_DIR="${LOCAL_INSTALL_DIR:-${DEMO_ROOT}/install/rknn_yolov5_demo_Linux}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rknn_demo}"
REMOTE_APP_DIR_NAME="${REMOTE_APP_DIR_NAME:-$(basename "${LOCAL_INSTALL_DIR}")}"
REMOTE_APP_ROOT="${REMOTE_ROOT}/${REMOTE_APP_DIR_NAME}"

REMOTE_MODEL="${REMOTE_MODEL:-model/RK3576/yolov5s-640-640.rknn}"
REMOTE_IMAGE="${REMOTE_IMAGE:-model/bus.jpg}"
REMOTE_OUTPUT="${REMOTE_OUTPUT:-out.jpg}"

usage()
{
    cat <<EOF
Usage: $0 <build|deploy|run|all>

This script adapts the current Omni3576 SDK's bundled RKNN object-detection demo.
It intentionally uses the SDK-shipped rknn_yolov5_demo because it is version-aligned
with the local RKNN runtime/toolchain set.

Environment:
  OMNI3576_SDK_ROOT=${SDK_ROOT}
  RKNN_DEMO_ROOT=${DEMO_ROOT}
  RKNN_GCC_COMPILER=${TOOLCHAIN_PREFIX}
  BOARD_HOST=${BOARD_HOST}
  BOARD_USER=${BOARD_USER}
  BOARD_PASSWORD=${BOARD_PASSWORD}
  REMOTE_ROOT=${REMOTE_ROOT}
EOF
}

run_remote()
{
    local command="$1"
    local command_b64
    command_b64="$(printf '%s' "${command}" | base64 -w0)"
    EXPECT_TARGET="${BOARD_USER}@${BOARD_HOST}" \
    EXPECT_PASSWORD="${BOARD_PASSWORD}" \
    EXPECT_COMMAND_B64="${command_b64}" \
    expect -c '
        set timeout -1
        set target $env(EXPECT_TARGET)
        set password $env(EXPECT_PASSWORD)
        set command_b64 $env(EXPECT_COMMAND_B64)
        spawn ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/camera_subsystem_known_hosts $target "echo $command_b64 | base64 -d | bash"
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
}

copy_to_board()
{
    local source_path="$1"
    local dest_path="$2"
    EXPECT_PASSWORD="${BOARD_PASSWORD}" \
    EXPECT_SOURCE="${source_path}" \
    EXPECT_DEST="${BOARD_USER}@${BOARD_HOST}:${dest_path}" \
    expect -c '
        set timeout -1
        set password $env(EXPECT_PASSWORD)
        set source $env(EXPECT_SOURCE)
        set dest $env(EXPECT_DEST)
        spawn scp -r -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/camera_subsystem_known_hosts $source $dest
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
}

copy_from_board()
{
    local source_path="$1"
    local dest_path="$2"
    mkdir -p "${dest_path}"
    EXPECT_PASSWORD="${BOARD_PASSWORD}" \
    EXPECT_SOURCE="${BOARD_USER}@${BOARD_HOST}:${source_path}" \
    EXPECT_DEST="${dest_path}" \
    expect -c '
        set timeout -1
        set password $env(EXPECT_PASSWORD)
        set source $env(EXPECT_SOURCE)
        set dest $env(EXPECT_DEST)
        spawn scp -r -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/camera_subsystem_known_hosts $source $dest
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
}

build_demo()
{
    if [[ ! -d "${DEMO_ROOT}" ]]; then
        echo "missing demo root: ${DEMO_ROOT}" >&2
        exit 1
    fi

    (
        cd "${DEMO_ROOT}"
        export GCC_COMPILER="${TOOLCHAIN_PREFIX}"
        ./build-linux.sh -t "${TARGET_SOC}" -a "${TARGET_ARCH}" -b "${BUILD_TYPE}"
    )

    if [[ ! -x "${LOCAL_INSTALL_DIR}/rknn_yolov5_demo" ]]; then
        echo "build output missing: ${LOCAL_INSTALL_DIR}/rknn_yolov5_demo" >&2
        exit 1
    fi
}

deploy_demo()
{
    if [[ ! -d "${LOCAL_INSTALL_DIR}" ]]; then
        echo "missing install dir: ${LOCAL_INSTALL_DIR}" >&2
        echo "run '$0 build' first" >&2
        exit 1
    fi

    run_remote "mkdir -p '${REMOTE_ROOT}' && rm -rf '${REMOTE_ROOT}'/*"
    copy_to_board "${LOCAL_INSTALL_DIR}/" "${REMOTE_ROOT}/"
}

run_demo()
{
    mkdir -p "${LOCAL_LOG_DIR}"

    run_remote "cd '${REMOTE_APP_ROOT}' && export LD_LIBRARY_PATH='./lib' && ./rknn_yolov5_demo '${REMOTE_MODEL}' '${REMOTE_IMAGE}' letterbox '${REMOTE_OUTPUT}' > run.log 2>&1"
    copy_from_board "${REMOTE_APP_ROOT}/run.log" "${LOCAL_LOG_DIR}"
    copy_from_board "${REMOTE_APP_ROOT}/${REMOTE_OUTPUT}" "${LOCAL_LOG_DIR}"

    echo "Board run log: ${LOCAL_LOG_DIR}/run.log"
    echo "Output image:   ${LOCAL_LOG_DIR}/${REMOTE_OUTPUT}"
}

action="${1:-all}"
case "${action}" in
    build)
        build_demo
        ;;
    deploy)
        deploy_demo
        ;;
    run)
        run_demo
        ;;
    all)
        build_demo
        deploy_demo
        run_demo
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
