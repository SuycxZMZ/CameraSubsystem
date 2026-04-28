#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${OMNI3576_SDK_ROOT:-${PROJECT_ROOT}/../Omni3576-sdk}"
BUILD_DIR="${CAMERA_SUBSYSTEM_RK3576_BUILD_DIR:-${PROJECT_ROOT}/build-rk3576}"
BUILD_TYPE="${CAMERA_SUBSYSTEM_BUILD_TYPE:-Release}"
OUTPUT_DIR="${CAMERA_SUBSYSTEM_RK3576_OUTPUT_DIR:-${PROJECT_ROOT}/bin/rk3576}"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/toolchains/rk3576.cmake" \
    -DOMNI3576_SDK_ROOT="${SDK_ROOT}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCAMERA_SUBSYSTEM_BUILD_TESTS=OFF \
    -DCAMERA_SUBSYSTEM_USE_SYSTEM_DEPS=OFF \
    -DCAMERA_SUBSYSTEM_BUILD_CODEC_SERVER=ON \
    -DCAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR="${OUTPUT_DIR}" \
    "$@"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "RK3576 binaries:"
BINARIES=(
    "${OUTPUT_DIR}/camera_publisher_example"
    "${OUTPUT_DIR}/camera_subscriber_example"
    "${OUTPUT_DIR}/camera_codec_server"
    "${OUTPUT_DIR}/dmabuf_smoke_test"
    "${OUTPUT_DIR}/mplane_dmabuf_probe"
)

if [[ -f "${OUTPUT_DIR}/rga_dmabuf_import_probe" ]]; then
    BINARIES+=("${OUTPUT_DIR}/rga_dmabuf_import_probe")
fi

if [[ -f "${OUTPUT_DIR}/mpp_dmabuf_import_probe" ]]; then
    BINARIES+=("${OUTPUT_DIR}/mpp_dmabuf_import_probe")
fi

if [[ -f "${OUTPUT_DIR}/mpp_jpeg_decode_probe" ]]; then
    BINARIES+=("${OUTPUT_DIR}/mpp_jpeg_decode_probe")
fi

file "${BINARIES[@]}"
