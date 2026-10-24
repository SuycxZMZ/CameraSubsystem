#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OFFICIAL_AI_ROOT="${OFFICIAL_AI_ROOT:-${PROJECT_ROOT}/../official-ai-latest}"
MODEL_ZOO_ROOT="${RKNN_MODEL_ZOO_ROOT:-${OFFICIAL_AI_ROOT}/rknn_model_zoo}"
SDK_ROOT="${OMNI3576_SDK_ROOT:-${PROJECT_ROOT}/../Omni3576-sdk}"
TOOLCHAIN_PREFIX="${RKNN_GCC_COMPILER:-${SDK_ROOT}/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu}"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem/rknn_official_demo}"

DEMO_NAME="${DEMO_NAME:-yolo11}"
MODEL_VARIANT="${MODEL_VARIANT:-${DEMO_NAME}n}"
TARGET_SOC="${TARGET_SOC:-rk3576}"
TARGET_ARCH="${TARGET_ARCH:-aarch64}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
MODEL_DTYPE="${MODEL_DTYPE:-i8}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rknn_official_${DEMO_NAME}}"
VENV_DIR="${VENV_DIR:-${PROJECT_ROOT}/.venv-rknn-official}"

EXAMPLE_ROOT="${MODEL_ZOO_ROOT}/examples/${DEMO_NAME}"
MODEL_DIR="${EXAMPLE_ROOT}/model"
PYTHON_DIR="${EXAMPLE_ROOT}/python"
DOWNLOAD_MODEL_SCRIPT="${MODEL_DIR}/download_model.sh"
CONVERT_SCRIPT="${PYTHON_DIR}/convert.py"
ONNX_MODEL_PATH="${ONNX_MODEL_PATH:-${MODEL_DIR}/${MODEL_VARIANT}.onnx}"
RKNN_MODEL_PATH="${RKNN_MODEL_PATH:-${MODEL_DIR}/${DEMO_NAME}.rknn}"

REMOTE_APP_ROOT="${REMOTE_ROOT}/rknn_${DEMO_NAME}_demo"
REMOTE_MODEL="${REMOTE_MODEL:-model/${DEMO_NAME}.rknn}"
REMOTE_IMAGE="${REMOTE_IMAGE:-model/bus.jpg}"
REMOTE_OUTPUT="${REMOTE_OUTPUT:-out.png}"

usage()
{
    cat <<EOF
Usage: $0 <sync|host-env|convert|build|deploy|run|all>

基于官方最新 RKNN Toolkit2 + Model Zoo，在主机侧完成模型转换、
交叉编译并把完整 demo 离线部署到 RK3576 板端运行。

默认示例：
  DEMO_NAME=${DEMO_NAME}
  MODEL_VARIANT=${MODEL_VARIANT}
  TARGET_SOC=${TARGET_SOC}
  TARGET_ARCH=${TARGET_ARCH}
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

resolve_install_dir()
{
    local install_dir="${MODEL_ZOO_ROOT}/install/${TARGET_SOC}_linux_${TARGET_ARCH}/rknn_${DEMO_NAME}_demo"
    if [[ ! -d "${install_dir}" ]]; then
        echo "missing install dir: ${install_dir}" >&2
        exit 1
    fi
    printf '%s\n' "${install_dir}"
}

sync_stack()
{
    "${PROJECT_ROOT}/scripts/sync-rknn-official-stack.sh"
}

setup_host_env()
{
    DEMO_NAME="${DEMO_NAME}" \
    OFFICIAL_AI_ROOT="${OFFICIAL_AI_ROOT}" \
    VENV_DIR="${VENV_DIR}" \
    "${PROJECT_ROOT}/scripts/setup-rknn-official-host-env.sh"
}

convert_model()
{
    if [[ ! -d "${EXAMPLE_ROOT}" ]]; then
        echo "missing example root: ${EXAMPLE_ROOT}" >&2
        echo "run '$0 sync' first" >&2
        exit 1
    fi

    if [[ ! -f "${ONNX_MODEL_PATH}" ]]; then
        if [[ ! -f "${DOWNLOAD_MODEL_SCRIPT}" ]]; then
            echo "missing model download script: ${DOWNLOAD_MODEL_SCRIPT}" >&2
            exit 1
        fi
        (
            cd "${MODEL_DIR}"
            bash ./download_model.sh
        )
    fi

    if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
        echo "missing RKNN official host venv: ${VENV_DIR}" >&2
        echo "run '$0 host-env' first" >&2
        exit 1
    fi

    (
        cd "${PYTHON_DIR}"
        "${VENV_DIR}/bin/python" "${CONVERT_SCRIPT}" "${ONNX_MODEL_PATH}" "${TARGET_SOC}" "${MODEL_DTYPE}" "${RKNN_MODEL_PATH}"
    )

    if [[ ! -f "${RKNN_MODEL_PATH}" ]]; then
        echo "missing RKNN model after conversion: ${RKNN_MODEL_PATH}" >&2
        exit 1
    fi
}

build_demo()
{
    if [[ ! -f "${RKNN_MODEL_PATH}" ]]; then
        echo "missing RKNN model: ${RKNN_MODEL_PATH}" >&2
        echo "run '$0 convert' first" >&2
        exit 1
    fi

    (
        cd "${MODEL_ZOO_ROOT}"
        export GCC_COMPILER="${TOOLCHAIN_PREFIX}"
        bash ./build-linux.sh -t "${TARGET_SOC}" -a "${TARGET_ARCH}" -d "${DEMO_NAME}" -b "${BUILD_TYPE}"
    )

    local install_dir
    install_dir="$(resolve_install_dir)"
    if [[ ! -x "${install_dir}/rknn_${DEMO_NAME}_demo" ]]; then
        echo "build output missing: ${install_dir}/rknn_${DEMO_NAME}_demo" >&2
        exit 1
    fi
}

deploy_demo()
{
    local install_dir
    install_dir="$(resolve_install_dir)"

    run_remote "mkdir -p '${REMOTE_ROOT}' && rm -rf '${REMOTE_APP_ROOT}' && mkdir -p '${REMOTE_ROOT}'"
    copy_to_board "${install_dir}/" "${REMOTE_ROOT}/"
}

run_demo()
{
    mkdir -p "${LOCAL_LOG_DIR}"

    run_remote "cd '${REMOTE_APP_ROOT}' && export LD_LIBRARY_PATH='./lib' && ./rknn_${DEMO_NAME}_demo '${REMOTE_MODEL}' '${REMOTE_IMAGE}' > run.log 2>&1"
    copy_from_board "${REMOTE_APP_ROOT}/run.log" "${LOCAL_LOG_DIR}"
    copy_from_board "${REMOTE_APP_ROOT}/${REMOTE_OUTPUT}" "${LOCAL_LOG_DIR}"

    echo "Board run log: ${LOCAL_LOG_DIR}/run.log"
    echo "Output image:   ${LOCAL_LOG_DIR}/${REMOTE_OUTPUT}"
}

action="${1:-all}"
case "${action}" in
    sync)
        sync_stack
        ;;
    host-env)
        setup_host_env
        ;;
    convert)
        convert_model
        ;;
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
        sync_stack
        setup_host_env
        convert_model
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
