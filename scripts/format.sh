#!/bin/bash

# CameraSubsystem 代码格式化脚本
# 使用项目根目录的 .clang-format 规则格式化 C/C++ 文件

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "Error: clang-format not found."
    echo "Install on Debian/Ubuntu:"
    echo "  sudo apt-get update && sudo apt-get install -y clang-format"
    exit 1
fi

MODE="${1:-all}"

echo "Project root: ${PROJECT_ROOT}"
echo "Formatting mode: ${MODE}"

if [ "${MODE}" = "changed" ]; then
    if ! command -v git >/dev/null 2>&1; then
        echo "Error: git not found."
        exit 1
    fi

    FILES_LIST="$(git -C "${PROJECT_ROOT}" diff --name-only --diff-filter=ACMR \
        | grep -E '\.(c|cc|cpp|cxx|h|hpp|hh)$' \
        | grep -v '^third_party/' \
        | sed "s#^#${PROJECT_ROOT}/#")"

    if [ -z "${FILES_LIST}" ]; then
        echo "No C/C++ files found."
        exit 0
    fi

    printf '%s\n' "${FILES_LIST}" | xargs -r clang-format -i
else
    FILE_COUNT="$(find "${PROJECT_ROOT}" \
        -path "${PROJECT_ROOT}/third_party" -prune -o \
        -type f \( -name "*.c" -o -name "*.cc" -o -name "*.cpp" -o -name "*.cxx" -o -name "*.h" -o -name "*.hpp" -o -name "*.hh" \) \
        -print | wc -l)"

    if [ "${FILE_COUNT}" -eq 0 ]; then
        echo "No C/C++ files found."
        exit 0
    fi

    find "${PROJECT_ROOT}" \
        -path "${PROJECT_ROOT}/third_party" -prune -o \
        -type f \( -name "*.c" -o -name "*.cc" -o -name "*.cpp" -o -name "*.cxx" -o -name "*.h" -o -name "*.hpp" -o -name "*.hh" \) \
        -print0 \
        | xargs -0 -r clang-format -i
fi

echo "Formatting completed."
