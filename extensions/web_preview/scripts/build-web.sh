#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$(dirname "$SCRIPT_DIR")/web"

echo "=== Building Web Preview Frontend ==="

cd "$WEB_DIR"

if ! command -v pnpm &>/dev/null; then
    echo "Error: pnpm is not installed. Please install pnpm first."
    exit 1
fi

echo "Installing dependencies..."
pnpm install --frozen-lockfile 2>/dev/null || pnpm install

echo "Building production bundle..."
pnpm build

echo "=== Build complete ==="
echo "Output: $WEB_DIR/dist/"
ls -lh dist/assets/
