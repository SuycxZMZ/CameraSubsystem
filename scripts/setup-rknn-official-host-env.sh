#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OFFICIAL_AI_ROOT="${OFFICIAL_AI_ROOT:-${PROJECT_ROOT}/../official-ai-latest}"
TOOLKIT_ROOT="${RKNN_TOOLKIT2_ROOT:-${OFFICIAL_AI_ROOT}/rknn-toolkit2}"
TOOLKIT_PACKAGE_DIR="${TOOLKIT_PACKAGE_DIR:-${TOOLKIT_ROOT}/rknn-toolkit2/packages/x86_64}"
MODEL_ZOO_ROOT="${RKNN_MODEL_ZOO_ROOT:-${OFFICIAL_AI_ROOT}/rknn_model_zoo}"
DEMO_NAME="${DEMO_NAME:-yolo11}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_DIR="${VENV_DIR:-${PROJECT_ROOT}/.venv-rknn-official}"
TOOLS_CACHE_DIR="${TOOLS_CACHE_DIR:-${PROJECT_ROOT}/.cache-tools}"
VIRTUALENV_PYZ="${VIRTUALENV_PYZ:-${TOOLS_CACHE_DIR}/virtualenv.pyz}"
RKNN_INSTALL_TORCH="${RKNN_INSTALL_TORCH:-cpu}"
TORCH_VERSION="${TORCH_VERSION:-2.4.0}"

usage()
{
    cat <<EOF
Usage: $0

为官方最新 RKNN Toolkit2 在主机侧建立轻量 venv。
不依赖 conda，默认适配当前开发机的 python3。

环境变量：
  OFFICIAL_AI_ROOT=${OFFICIAL_AI_ROOT}
  RKNN_TOOLKIT2_ROOT=${TOOLKIT_ROOT}
  TOOLKIT_PACKAGE_DIR=${TOOLKIT_PACKAGE_DIR}
  RKNN_MODEL_ZOO_ROOT=${MODEL_ZOO_ROOT}
  DEMO_NAME=${DEMO_NAME}
  PYTHON_BIN=${PYTHON_BIN}
  VENV_DIR=${VENV_DIR}
  TOOLS_CACHE_DIR=${TOOLS_CACHE_DIR}
  VIRTUALENV_PYZ=${VIRTUALENV_PYZ}
  RKNN_INSTALL_TORCH=${RKNN_INSTALL_TORCH}
  TORCH_VERSION=${TORCH_VERSION}
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ ! -d "${TOOLKIT_ROOT}" ]]; then
    echo "missing toolkit root: ${TOOLKIT_ROOT}" >&2
    echo "run ./scripts/sync-rknn-official-stack.sh first" >&2
    exit 1
fi

if [[ ! -d "${MODEL_ZOO_ROOT}" ]]; then
    echo "missing model zoo root: ${MODEL_ZOO_ROOT}" >&2
    echo "run ./scripts/sync-rknn-official-stack.sh first" >&2
    exit 1
fi

if [[ ! -d "${TOOLKIT_PACKAGE_DIR}" ]]; then
    echo "missing toolkit package dir: ${TOOLKIT_PACKAGE_DIR}" >&2
    exit 1
fi

PYTHON_TAG="$("${PYTHON_BIN}" - <<'PY'
import sys
print(f"cp{sys.version_info.major}{sys.version_info.minor}")
PY
)"

WHEEL_FILE="$(find "${TOOLKIT_PACKAGE_DIR}" -maxdepth 1 -type f -name 'rknn_toolkit2-*.whl' | grep "${PYTHON_TAG}" | sort | tail -n 1 || true)"
EXAMPLE_REQUIREMENTS_FILE="$(find "${MODEL_ZOO_ROOT}/examples/${DEMO_NAME}" -type f -name 'requirements*.txt' | sort | head -n 1 || true)"

if [[ -z "${WHEEL_FILE}" ]]; then
    echo "no RKNN Toolkit2 wheel matched ${PYTHON_TAG} under ${TOOLKIT_ROOT}" >&2
    echo "please confirm the synced official toolkit version supports current python" >&2
    exit 1
fi

create_venv()
{
    if "${PYTHON_BIN}" -m venv "${VENV_DIR}" >/tmp/camera_subsystem_rknn_venv.log 2>&1; then
        return 0
    fi

    mkdir -p "${TOOLS_CACHE_DIR}"
    if [[ ! -f "${VIRTUALENV_PYZ}" ]]; then
        curl -fL --retry 3 --retry-delay 1 \
            -H 'User-Agent: CameraSubsystem-RKNN-HostEnv' \
            https://bootstrap.pypa.io/virtualenv.pyz \
            -o "${VIRTUALENV_PYZ}"
    fi

    "${PYTHON_BIN}" "${VIRTUALENV_PYZ}" "${VENV_DIR}"
}

create_venv
source "${VENV_DIR}/bin/activate"

python -m pip install --no-deps "${WHEEL_FILE}"
python -m pip install \
    "numpy<=1.26.4" \
    "protobuf>=4.21.6,<=4.25.4" \
    "psutil>=5.9.0" \
    "ruamel.yaml>=0.17.21" \
    "scipy>=1.9.3" \
    "tqdm>=4.64.1" \
    "opencv-python>=4.5.5.64" \
    "fast-histogram>=0.11" \
    "onnx==1.16.1" \
    "onnxruntime>=1.17.0" \
    "ml_dtypes>=0.5.0" \
    "flatbuffers" \
    "packaging"

if [[ -n "${EXAMPLE_REQUIREMENTS_FILE}" ]]; then
    python -m pip install -r "${EXAMPLE_REQUIREMENTS_FILE}"
fi

if [[ "${RKNN_INSTALL_TORCH}" == "cpu" ]]; then
    python -m pip install --index-url https://download.pytorch.org/whl/cpu "torch==${TORCH_VERSION}"
elif [[ "${RKNN_INSTALL_TORCH}" == "full" ]]; then
    python -m pip install "torch==${TORCH_VERSION}"
fi

cat <<EOF
RKNN official host venv ready:
  ${VENV_DIR}

Activate with:
  source "${VENV_DIR}/bin/activate"

Installed wheel:
  ${WHEEL_FILE}

Torch policy:
  RKNN_INSTALL_TORCH=${RKNN_INSTALL_TORCH}
EOF
