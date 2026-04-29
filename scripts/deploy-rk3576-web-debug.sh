#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"

BIN_DIR="${PROJECT_ROOT}/bin/rk3576"
GATEWAY_BIN="${PROJECT_ROOT}/extensions/web_preview/gateway/build-rk3576/web_preview_gateway"
WEB_DIST="${PROJECT_ROOT}/extensions/web_preview/web/dist"

remote_bin="${REMOTE_ROOT}/bin"
remote_web="${REMOTE_ROOT}/web_preview/dist"
remote_scripts="${REMOTE_ROOT}/scripts"
remote_logs="${REMOTE_ROOT}/logs"
remote_run="${REMOTE_ROOT}/run"
remote_tmp="${REMOTE_ROOT}/tmp"
remote_recordings="${REMOTE_ROOT}/recordings"

echo "Deploying CameraSubsystem web debug stack to ${BOARD_USER}@${BOARD_HOST}:${REMOTE_ROOT}"

ssh "${BOARD_USER}@${BOARD_HOST}" \
    "mkdir -p '${remote_bin}' '${remote_web}' '${remote_scripts}' '${remote_logs}' '${remote_run}' '${remote_tmp}' '${remote_recordings}'"
ssh "${BOARD_USER}@${BOARD_HOST}" "rm -rf '${remote_web}'/*"

required_bins=(
    "${BIN_DIR}/camera_publisher_example"
    "${BIN_DIR}/camera_codec_server"
)

for bin in "${required_bins[@]}"; do
    if [[ ! -f "${bin}" ]]; then
        echo "missing binary: ${bin}"
        echo "run ./scripts/build-rk3576.sh first"
        exit 1
    fi
done

if [[ ! -f "${GATEWAY_BIN}" ]]; then
    echo "missing gateway binary: ${GATEWAY_BIN}"
    echo "run extensions/web_preview/scripts/build-gateway-rk3576.sh first"
    exit 1
fi

if [[ ! -d "${WEB_DIST}" ]]; then
    echo "missing web dist: ${WEB_DIST}"
    echo "run extensions/web_preview/scripts/build-web.sh first"
    exit 1
fi

scp \
    "${BIN_DIR}/camera_publisher_example" \
    "${BIN_DIR}/camera_codec_server" \
    "${GATEWAY_BIN}" \
    "${BOARD_USER}@${BOARD_HOST}:${remote_bin}/"

scp \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-v1-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-stability-test-rk3576.sh" \
    "${BOARD_USER}@${BOARD_HOST}:${remote_scripts}/"

scp -r "${WEB_DIST}/"* "${BOARD_USER}@${BOARD_HOST}:${remote_web}/"

ssh "${BOARD_USER}@${BOARD_HOST}" \
    "chmod +x '${remote_bin}/camera_publisher_example' '${remote_bin}/camera_codec_server' '${remote_bin}/web_preview_gateway' '${remote_scripts}/codec-v1-smoke-rk3576.sh' '${remote_scripts}/codec-stability-test-rk3576.sh'"

echo "Deploy complete."
echo "Board root: ${REMOTE_ROOT}"
echo "Web URL: http://${BOARD_HOST}:8080"
