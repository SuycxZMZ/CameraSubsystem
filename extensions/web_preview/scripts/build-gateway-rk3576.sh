#!/bin/bash

set -euo pipefail

EXT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "${EXT_ROOT}/../.." && pwd)"
GATEWAY_ROOT="${EXT_ROOT}/gateway"
SDK_ROOT="${OMNI3576_SDK_ROOT:-${PROJECT_ROOT}/../Omni3576-sdk}"
BUILD_DIR="${WEB_PREVIEW_GATEWAY_RK3576_BUILD_DIR:-${GATEWAY_ROOT}/build-rk3576}"
BUILD_TYPE="${WEB_PREVIEW_GATEWAY_BUILD_TYPE:-Release}"
TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/toolchains/rk3576.cmake"

cmake -S "${GATEWAY_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DOMNI3576_SDK_ROOT="${SDK_ROOT}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "$@"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "RK3576 Gateway binary:"
file "${BUILD_DIR}/web_preview_gateway"
