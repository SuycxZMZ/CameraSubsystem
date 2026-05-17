#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SKIP_BUILD_CORE="${SKIP_BUILD_CORE:-0}"
SKIP_BUILD_GATEWAY="${SKIP_BUILD_GATEWAY:-0}"
SKIP_BUILD_WEB="${SKIP_BUILD_WEB:-0}"
SKIP_DEPLOY="${SKIP_DEPLOY:-0}"
START_AFTER_DEPLOY="${START_AFTER_DEPLOY:-1}"

usage()
{
    cat <<EOF
Usage: $0

One command from CameraSubsystem/ to:
  1. build RK3576 binaries
  2. build web_preview gateway
  3. build web frontend
  4. deploy to board
  5. restart board debug stack

Environment:
  SKIP_BUILD_CORE=${SKIP_BUILD_CORE}
  SKIP_BUILD_GATEWAY=${SKIP_BUILD_GATEWAY}
  SKIP_BUILD_WEB=${SKIP_BUILD_WEB}
  SKIP_DEPLOY=${SKIP_DEPLOY}
  START_AFTER_DEPLOY=${START_AFTER_DEPLOY}
  BOARD_HOST=${BOARD_HOST:-192.168.31.9}
  BOARD_USER=${BOARD_USER:-luckfox}
  BOARD_PASSWORD=${BOARD_PASSWORD:-luckfox}
  REMOTE_ROOT=${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

cd "${PROJECT_ROOT}"

if [[ "${SKIP_BUILD_CORE}" != "1" ]]; then
    ./scripts/build-rk3576.sh
fi

if [[ "${SKIP_BUILD_GATEWAY}" != "1" ]]; then
    ./extensions/web_preview/scripts/build-gateway-rk3576.sh
fi

if [[ "${SKIP_BUILD_WEB}" != "1" ]]; then
    ./extensions/web_preview/scripts/build-web.sh
fi

if [[ "${SKIP_DEPLOY}" != "1" ]]; then
    ./scripts/deploy-rk3576-web-debug.sh
fi

if [[ "${START_AFTER_DEPLOY}" == "1" ]]; then
    ./scripts/rk3576-run-web-stack.sh restart
    echo
    echo "Open in browser: http://${BOARD_HOST:-192.168.31.9}:${HTTP_PORT:-8080}"
fi
