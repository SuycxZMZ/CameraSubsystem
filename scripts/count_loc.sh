#!/bin/bash

# CameraSubsystem 代码量统计脚本
# 默认统计 C/C++ 代码，且排除 third_party 与构建产物目录

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-cpp}"  # cpp | all

EXCLUDE_DIRS="third_party,.git,build,bin,.cache,.codeartsdoer"
CPP_EXTS="c,cc,cpp,cxx,h,hpp,hh"
ALL_EXTS="c,cc,cpp,cxx,h,hpp,hh,cmake,md,txt,sh,py,json,yml,yaml"

function print_usage() {
    echo "用法:"
    echo "  ./scripts/count_loc.sh [cpp|all]"
    echo ""
    echo "模式:"
    echo "  cpp  统计 C/C++ 代码（默认）"
    echo "  all  统计常见文本源码与文档"
}

function find_cpp_files() {
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/third_party" \
        -o -path "${PROJECT_ROOT}/.git" \
        -o -path "${PROJECT_ROOT}/build" \
        -o -path "${PROJECT_ROOT}/bin" \
        -o -path "${PROJECT_ROOT}/.cache" \
        -o -path "${PROJECT_ROOT}/.codeartsdoer" \) -prune -o \
        -type f \( -name "*.c" -o -name "*.cc" -o -name "*.cpp" -o -name "*.cxx" \
        -o -name "*.h" -o -name "*.hpp" -o -name "*.hh" \) \
        -print0
}

function find_all_files() {
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/third_party" \
        -o -path "${PROJECT_ROOT}/.git" \
        -o -path "${PROJECT_ROOT}/build" \
        -o -path "${PROJECT_ROOT}/bin" \
        -o -path "${PROJECT_ROOT}/.cache" \
        -o -path "${PROJECT_ROOT}/.codeartsdoer" \) -prune -o \
        -type f \( -name "*.c" -o -name "*.cc" -o -name "*.cpp" -o -name "*.cxx" \
        -o -name "*.h" -o -name "*.hpp" -o -name "*.hh" -o -name "*.cmake" \
        -o -name "*.md" -o -name "*.txt" -o -name "*.sh" -o -name "*.py" \
        -o -name "*.json" -o -name "*.yml" -o -name "*.yaml" \
        -o -name "CMakeLists.txt" \) \
        -print0
}

if [[ "${MODE}" != "cpp" && "${MODE}" != "all" ]]; then
    print_usage
    exit 1
fi

echo "Project root: ${PROJECT_ROOT}"
echo "统计模式: ${MODE}"
echo "排除目录: ${EXCLUDE_DIRS}"
echo ""

# 优先使用 cloc（如果已安装）
if command -v cloc >/dev/null 2>&1; then
    echo "使用 cloc 统计..."
    if [[ "${MODE}" == "cpp" ]]; then
        cloc "${PROJECT_ROOT}" \
            --exclude-dir="${EXCLUDE_DIRS}" \
            --include-ext="${CPP_EXTS}"
    else
        cloc "${PROJECT_ROOT}" \
            --exclude-dir="${EXCLUDE_DIRS}" \
            --include-ext="${ALL_EXTS}"
    fi
    exit 0
fi

echo "未检测到 cloc，回退到 find + wc 统计。"
echo "如需更详细统计，可安装 cloc: sudo apt-get install -y cloc"
echo ""

declare -A ext_file_count=()
declare -A ext_line_count=()
total_files=0
total_lines=0

if [[ "${MODE}" == "cpp" ]]; then
    file_stream_cmd=find_cpp_files
else
    file_stream_cmd=find_all_files
fi

while IFS= read -r -d '' file; do
    # wc 输出含前导空格，去除空白
    lines="$(wc -l < "${file}")"
    lines="${lines//[[:space:]]/}"

    ext="${file##*.}"
    ext="${ext,,}"
    if [[ "${file}" == *"/CMakeLists.txt" ]]; then
        ext="cmakelists"
    fi

    ext_file_count["${ext}"]=$(( ${ext_file_count["${ext}"]:-0} + 1 ))
    ext_line_count["${ext}"]=$(( ${ext_line_count["${ext}"]:-0} + lines ))
    total_files=$(( total_files + 1 ))
    total_lines=$(( total_lines + lines ))
done < <(${file_stream_cmd})

echo "按扩展名统计:"
printf "%-14s %-10s %-10s\n" "Extension" "Files" "Lines"
printf "%-14s %-10s %-10s\n" "---------" "-----" "-----"
for ext in "${!ext_file_count[@]}"; do
    printf "%-14s %-10d %-10d\n" ".${ext}" "${ext_file_count[${ext}]}" "${ext_line_count[${ext}]}"
done | sort

echo ""
echo "总计:"
echo "  Files: ${total_files}"
echo "  Lines: ${total_lines}"
