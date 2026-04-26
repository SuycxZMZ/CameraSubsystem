#!/bin/bash

set -euo pipefail

EXT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GATEWAY_ROOT="${EXT_ROOT}/gateway"
BUILD_DIR="${WEB_PREVIEW_GATEWAY_BUILD_DIR:-${GATEWAY_ROOT}/build}"
BUILD_TYPE="${WEB_PREVIEW_GATEWAY_BUILD_TYPE:-Debug}"

cmake -S "${GATEWAY_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "$@"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "Gateway binary:"
file "${BUILD_DIR}/web_preview_gateway"
