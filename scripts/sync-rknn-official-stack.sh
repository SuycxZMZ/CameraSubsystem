#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OFFICIAL_AI_ROOT="${OFFICIAL_AI_ROOT:-${PROJECT_ROOT}/../official-ai-latest}"
GITHUB_API_ROOT="${GITHUB_API_ROOT:-https://api.github.com}"
RKNN_TOOLKIT2_REF="${RKNN_TOOLKIT2_REF:-}"
RKNN_MODEL_ZOO_REF="${RKNN_MODEL_ZOO_REF:-}"
RKNN_LLM_REF="${RKNN_LLM_REF:-}"
SYNC_RKNN_LLM="${SYNC_RKNN_LLM:-0}"
CURL_EXTRA_ARGS="${CURL_EXTRA_ARGS:-}"

usage()
{
    cat <<EOF
Usage: $0

在主机侧同步 Rockchip 官方最新 AI 栈镜像目录。
本脚本只在开发机执行，不在板端运行。

默认同步内容：
  - airockchip/rknn-toolkit2      （latest release）
  - airockchip/rknn_model_zoo     （latest release）

可选同步内容：
  - airockchip/rknn-llm           （latest release，仅在 SYNC_RKNN_LLM=1 时启用）

环境变量：
  OFFICIAL_AI_ROOT=${OFFICIAL_AI_ROOT}
  GITHUB_API_ROOT=${GITHUB_API_ROOT}
  RKNN_TOOLKIT2_REF=${RKNN_TOOLKIT2_REF:-<latest release>}
  RKNN_MODEL_ZOO_REF=${RKNN_MODEL_ZOO_REF:-<latest release>}
  SYNC_RKNN_LLM=${SYNC_RKNN_LLM}
  RKNN_LLM_REF=${RKNN_LLM_REF:-<latest release, only when SYNC_RKNN_LLM=1>}
  CURL_EXTRA_ARGS=${CURL_EXTRA_ARGS:-<empty>}
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

mkdir -p "${OFFICIAL_AI_ROOT}"

read -r -a CURL_ARGS_ARR <<< "${CURL_EXTRA_ARGS}"

PROXY_HINT="${https_proxy:-${HTTPS_PROXY:-${http_proxy:-${HTTP_PROXY:-}}}}"
if [[ -n "${PROXY_HINT}" && "${PROXY_HINT}" == *"127.0.0.1"* ]]; then
    cat >&2 <<EOF
note: detected local proxy from shell environment: ${PROXY_HINT}
      if this proxy is not running, sync will fail.
      you can either:
        1) start the proxy service
        2) export a working HTTPS_PROXY / HTTP_PROXY
        3) run with CURL_EXTRA_ARGS="--noproxy *" when direct DNS/network is available
EOF
fi

fetch_latest_tag()
{
    local repo_slug="$1"
    curl -fsSL \
        "${CURL_ARGS_ARR[@]}" \
        -H 'Accept: application/vnd.github+json' \
        -H 'User-Agent: CameraSubsystem-RKNN-Sync' \
        "${GITHUB_API_ROOT}/repos/${repo_slug}/releases/latest" \
        | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' \
        | head -n 1
}

download_and_extract()
{
    local repo_slug="$1"
    local repo_name="$2"
    local ref_type="$3"
    local ref_name="$4"
    local archive_url="$5"
    local repo_root="${OFFICIAL_AI_ROOT}/${repo_name}"
    local tmp_dir
    local archive_path
    local extracted_root

    tmp_dir="$(mktemp -d)"
    archive_path="${tmp_dir}/${repo_name}.tar.gz"

    curl -fL --retry 3 --retry-delay 1 \
        "${CURL_ARGS_ARR[@]}" \
        -H 'User-Agent: CameraSubsystem-RKNN-Sync' \
        "${archive_url}" \
        -o "${archive_path}"

    tar -xzf "${archive_path}" -C "${tmp_dir}"
    extracted_root="$(find "${tmp_dir}" -mindepth 1 -maxdepth 1 -type d ! -name "$(basename "${archive_path}")" | head -n 1)"

    if [[ -z "${extracted_root}" || ! -d "${extracted_root}" ]]; then
        echo "extract failed for ${repo_slug}" >&2
        rm -rf "${tmp_dir}"
        exit 1
    fi

    rm -rf "${repo_root}.tmp"
    mv "${extracted_root}" "${repo_root}.tmp"
    rm -rf "${repo_root}"
    mv "${repo_root}.tmp" "${repo_root}"

    cat > "${repo_root}/.camera_subsystem_sync_meta" <<EOF
repo=${repo_slug}
ref_type=${ref_type}
ref_name=${ref_name}
archive_url=${archive_url}
synced_at=$(date '+%Y-%m-%d %H:%M:%S %z')
EOF

    rm -rf "${tmp_dir}"
}

sync_repo()
{
    local repo_name="$1"
    local repo_slug="$2"
    local default_branch="$3"
    local pinned_ref="$4"
    local tag_name
    local archive_url

    tag_name="${pinned_ref}"
    if [[ -z "${tag_name}" ]]; then
        tag_name="$(fetch_latest_tag "${repo_slug}" || true)"
    fi

    if [[ -n "${tag_name}" ]]; then
        archive_url="https://github.com/${repo_slug}/archive/refs/tags/${tag_name}.tar.gz"
        download_and_extract "${repo_slug}" "${repo_name}" "tag" "${tag_name}" "${archive_url}"
        echo "[sync] ${repo_name}: ${tag_name}"
    else
        archive_url="https://github.com/${repo_slug}/archive/refs/heads/${default_branch}.tar.gz"
        download_and_extract "${repo_slug}" "${repo_name}" "branch" "${default_branch}" "${archive_url}"
        echo "[sync] ${repo_name}: ${default_branch}"
    fi
}

sync_repo "rknn-toolkit2" "airockchip/rknn-toolkit2" "master" "${RKNN_TOOLKIT2_REF}"
sync_repo "rknn_model_zoo" "airockchip/rknn_model_zoo" "main" "${RKNN_MODEL_ZOO_REF}"

if [[ "${SYNC_RKNN_LLM}" == "1" ]]; then
    sync_repo "rknn-llm" "airockchip/rknn-llm" "main" "${RKNN_LLM_REF}"
else
    echo "[skip] rknn-llm: disabled (set SYNC_RKNN_LLM=1 to enable)"
fi

echo "Official AI stack synced under: ${OFFICIAL_AI_ROOT}"
