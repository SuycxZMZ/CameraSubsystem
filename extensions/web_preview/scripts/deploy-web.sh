#!/usr/bin/env bash
set -euo pipefail

BOARD_IP="${1:-192.168.31.9}"
BOARD_USER="${2:-luckfox}"
REMOTE_DIR="${3:-/home/luckfox/web_preview/web/dist}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$(dirname "$SCRIPT_DIR")/web"
DIST_DIR="$WEB_DIR/dist"

echo "=== Deploying Web Preview Frontend to Board ==="

if [ ! -d "$DIST_DIR" ]; then
    echo "Error: dist/ directory not found. Run build-web.sh first."
    exit 1
fi

echo "Deploying to ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}"
scp -r "$DIST_DIR"/* "${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/"

echo "=== Deploy complete ==="
echo "Access: http://${BOARD_IP}:8080"
