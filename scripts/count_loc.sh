#!/usr/bin/env bash

# CameraSubsystem 代码量统计脚本
# 默认统计工程源码：
#   - C/C++ / Java / Kotlin / AIDL / Proto
#   - 前端源码 JS / TS / Vue / HTML / CSS 等
# 不统计：
#   - CMake / Make / Shell
#   - Markdown / TXT / JSON / YAML 等文档或配置文件
#   - third_party / node_modules / build / dist 等第三方或构建产物目录

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

RAW_MODE="${1:-code}"  # code | cpp
MODE="${RAW_MODE}"

# 兼容旧用法：原来的 all 现在等价于 code
if [[ "${MODE}" == "all" ]]; then
    MODE="code"
fi

EXCLUDE_DIRS="third_party,.git,build,bin,out,.cache,.codeartsdoer,cmake-build-debug,cmake-build-release,node_modules,dist,coverage,.next,.nuxt,.vite,.turbo,.output,target"

CPP_EXTS="c,cc,cpp,cxx,h,hpp,hh,hxx"

# 工程源码后缀：
# 不包含 cmake / mk / make / sh / md / txt / json / yml / yaml
CODE_EXTS="c,cc,cpp,cxx,h,hpp,hh,hxx,java,kt,aidl,proto,js,jsx,ts,tsx,mjs,cjs,vue,svelte,html,css,scss,sass,less,rs,go"

function print_usage()
{
    echo "用法:"
    echo "  ./scripts/count_loc.sh [code|cpp]"
    echo ""
    echo "模式:"
    echo "  code  统计工程源码（默认），包含前端源码，不包含 CMake / Make / Shell / 文档 / 配置文件"
    echo "  cpp   仅统计 C/C++ 源码"
    echo ""
    echo "兼容:"
    echo "  all   等价于 code"
}

function find_cpp_files()
{
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/third_party" \
        -o -path "${PROJECT_ROOT}/.git" \
        -o -path "${PROJECT_ROOT}/build" \
        -o -path "${PROJECT_ROOT}/bin" \
        -o -path "${PROJECT_ROOT}/out" \
        -o -path "${PROJECT_ROOT}/.cache" \
        -o -path "${PROJECT_ROOT}/.codeartsdoer" \
        -o -path "${PROJECT_ROOT}/cmake-build-debug" \
        -o -path "${PROJECT_ROOT}/cmake-build-release" \
        -o -path "${PROJECT_ROOT}/node_modules" \
        -o -path "${PROJECT_ROOT}/dist" \
        -o -path "${PROJECT_ROOT}/coverage" \
        -o -path "${PROJECT_ROOT}/.next" \
        -o -path "${PROJECT_ROOT}/.nuxt" \
        -o -path "${PROJECT_ROOT}/.vite" \
        -o -path "${PROJECT_ROOT}/.turbo" \
        -o -path "${PROJECT_ROOT}/.output" \
        -o -path "${PROJECT_ROOT}/target" \) -prune -o \
        -type f \( \
        -name "*.c" \
        -o -name "*.cc" \
        -o -name "*.cpp" \
        -o -name "*.cxx" \
        -o -name "*.h" \
        -o -name "*.hpp" \
        -o -name "*.hh" \
        -o -name "*.hxx" \
        \) \
        -print0
}

function find_code_files()
{
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/third_party" \
        -o -path "${PROJECT_ROOT}/.git" \
        -o -path "${PROJECT_ROOT}/build" \
        -o -path "${PROJECT_ROOT}/bin" \
        -o -path "${PROJECT_ROOT}/out" \
        -o -path "${PROJECT_ROOT}/.cache" \
        -o -path "${PROJECT_ROOT}/.codeartsdoer" \
        -o -path "${PROJECT_ROOT}/cmake-build-debug" \
        -o -path "${PROJECT_ROOT}/cmake-build-release" \
        -o -path "${PROJECT_ROOT}/node_modules" \
        -o -path "${PROJECT_ROOT}/dist" \
        -o -path "${PROJECT_ROOT}/coverage" \
        -o -path "${PROJECT_ROOT}/.next" \
        -o -path "${PROJECT_ROOT}/.nuxt" \
        -o -path "${PROJECT_ROOT}/.vite" \
        -o -path "${PROJECT_ROOT}/.turbo" \
        -o -path "${PROJECT_ROOT}/.output" \
        -o -path "${PROJECT_ROOT}/target" \) -prune -o \
        -type f \( \
        -name "*.c" \
        -o -name "*.cc" \
        -o -name "*.cpp" \
        -o -name "*.cxx" \
        -o -name "*.h" \
        -o -name "*.hpp" \
        -o -name "*.hh" \
        -o -name "*.hxx" \
        -o -name "*.java" \
        -o -name "*.kt" \
        -o -name "*.aidl" \
        -o -name "*.proto" \
        -o -name "*.js" \
        -o -name "*.jsx" \
        -o -name "*.ts" \
        -o -name "*.tsx" \
        -o -name "*.mjs" \
        -o -name "*.cjs" \
        -o -name "*.vue" \
        -o -name "*.svelte" \
        -o -name "*.html" \
        -o -name "*.css" \
        -o -name "*.scss" \
        -o -name "*.sass" \
        -o -name "*.less" \
        -o -name "*.rs" \
        -o -name "*.go" \
        \) \
        -print0
}

if [[ "${MODE}" != "code" && "${MODE}" != "cpp" ]]; then
    print_usage
    exit 1
fi

echo "Project root: ${PROJECT_ROOT}"
echo "统计模式: ${MODE}"
echo "排除目录: ${EXCLUDE_DIRS}"
echo ""

# 优先使用 cloc
if command -v cloc >/dev/null 2>&1; then
    echo "使用 cloc 统计..."

    if [[ "${MODE}" == "cpp" ]]; then
        cloc "${PROJECT_ROOT}" \
            --exclude-dir="${EXCLUDE_DIRS}" \
            --include-ext="${CPP_EXTS}"
    else
        cloc "${PROJECT_ROOT}" \
            --exclude-dir="${EXCLUDE_DIRS}" \
            --include-ext="${CODE_EXTS}"
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
    file_stream_cmd=find_code_files
fi

while IFS= read -r -d '' file; do
    lines="$(wc -l < "${file}")"
    lines="${lines//[[:space:]]/}"

    ext="${file##*.}"
    ext="${ext,,}"

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