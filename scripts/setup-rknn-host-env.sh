#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${OMNI3576_SDK_ROOT:-${PROJECT_ROOT}/../Omni3576-sdk}"
TOOLKIT_ROOT="${RKNN_TOOLKIT2_ROOT:-${SDK_ROOT}/external/rknn-toolkit2/rknn-toolkit2}"
PYTHON_BIN="${PYTHON_BIN:-}"

usage()
{
    cat <<EOF
Usage: $0

Create a lightweight host venv for RKNN model conversion without conda.

Environment:
  OMNI3576_SDK_ROOT=${SDK_ROOT}
  RKNN_TOOLKIT2_ROOT=${TOOLKIT_ROOT}
  PYTHON_BIN=<python3.10|python3.11 path>

Notes:
  - Current Omni3576 SDK bundles RKNN Toolkit2 2.0.0b0 wheels for cp310/cp311.
  - Python 3.12 is not supported by the SDK-bundled wheel set.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ ! -d "${TOOLKIT_ROOT}/packages" ]]; then
    echo "missing toolkit packages: ${TOOLKIT_ROOT}/packages" >&2
    exit 1
fi

if [[ -z "${PYTHON_BIN}" ]]; then
    if command -v python3.11 >/dev/null 2>&1; then
        PYTHON_BIN="$(command -v python3.11)"
    elif command -v python3.10 >/dev/null 2>&1; then
        PYTHON_BIN="$(command -v python3.10)"
    else
        echo "No compatible python found." >&2
        echo "Current SDK requires python3.10 or python3.11 for host RKNN Toolkit2 2.0.0b0." >&2
        echo "Install one of them, then rerun with PYTHON_BIN=/path/to/python3.11 $0" >&2
        exit 2
    fi
fi

PYTHON_VER="$("${PYTHON_BIN}" - <<'PY'
import sys
print(f"cp{sys.version_info.major}{sys.version_info.minor}")
PY
)"

case "${PYTHON_VER}" in
    cp310|cp311)
        ;;
    *)
        echo "Unsupported python ABI for current SDK wheel set: ${PYTHON_VER}" >&2
        echo "Use python3.10 or python3.11." >&2
        exit 2
        ;;
esac

REQ_FILE="$(find "${TOOLKIT_ROOT}/packages" -maxdepth 1 -name "requirements_${PYTHON_VER}-*.txt" | head -1)"
WHEEL_FILE="$(find "${TOOLKIT_ROOT}/packages" -maxdepth 1 -name "rknn_toolkit2-*.whl" | grep "${PYTHON_VER}" | head -1)"

if [[ -z "${REQ_FILE}" || -z "${WHEEL_FILE}" ]]; then
    echo "Missing requirement or wheel file for ${PYTHON_VER} under ${TOOLKIT_ROOT}/packages" >&2
    exit 1
fi

VENV_DIR="${PROJECT_ROOT}/.venv-rknn-${PYTHON_VER}"

"${PYTHON_BIN}" -m venv "${VENV_DIR}"
source "${VENV_DIR}/bin/activate"
python -m pip install --upgrade pip setuptools wheel
python -m pip install -r "${REQ_FILE}"
python -m pip install "${WHEEL_FILE}"

echo "RKNN host venv ready: ${VENV_DIR}"
echo "Activate with: source ${VENV_DIR}/bin/activate"
