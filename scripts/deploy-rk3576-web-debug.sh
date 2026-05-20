#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/luckfox/CameraSubsystem}"
STOP_BEFORE_DEPLOY="${STOP_BEFORE_DEPLOY:-1}"

BIN_DIR="${PROJECT_ROOT}/bin/rk3576"
GATEWAY_BIN="${PROJECT_ROOT}/extensions/web_preview/gateway/build-rk3576/web_preview_gateway"
WEB_DIST="${PROJECT_ROOT}/extensions/web_preview/web/dist"
OFFICIAL_AI_ROOT="${OFFICIAL_AI_ROOT:-${PROJECT_ROOT}/../official-ai-latest}"
LOCAL_MODEL_DIR="${LOCAL_MODEL_DIR:-${OFFICIAL_AI_ROOT}/rknn_model_zoo/install/rk3576_linux_aarch64/rknn_yolo11_demo/model}"
LOCAL_DETECTION_MODEL="${LOCAL_DETECTION_MODEL:-${LOCAL_MODEL_DIR}/yolo11.rknn}"
LOCAL_DETECTION_LABELS="${LOCAL_DETECTION_LABELS:-${LOCAL_MODEL_DIR}/coco_80_labels_list.txt}"

remote_bin="${REMOTE_ROOT}/bin"
remote_web="${REMOTE_ROOT}/web_preview/dist"
remote_scripts="${REMOTE_ROOT}/scripts"
remote_logs="${REMOTE_ROOT}/logs"
remote_run="${REMOTE_ROOT}/run"
remote_tmp="${REMOTE_ROOT}/tmp"
remote_recordings="${REMOTE_ROOT}/recordings"
remote_models="${REMOTE_ROOT}/models"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

run_ssh()
{
    local command="$1"
    if command -v sshpass >/dev/null 2>&1; then
        sshpass -p "${BOARD_PASSWORD}" ssh "${SSH_OPTS[@]}" "${TARGET}" "${command}"
    elif command -v expect >/dev/null 2>&1; then
        EXPECT_TARGET="${TARGET}" EXPECT_PASSWORD="${BOARD_PASSWORD}" EXPECT_COMMAND="${command}" \
        expect -c '
            set timeout -1
            set target $env(EXPECT_TARGET)
            set password $env(EXPECT_PASSWORD)
            set command $env(EXPECT_COMMAND)
            spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $target $command
            expect {
                -re "(?i)password:" {
                    send "$password\r"
                    exp_continue
                }
                eof
            }
            catch wait result
            exit [lindex $result 3]
        '
    else
        ssh "${SSH_OPTS[@]}" "${TARGET}" "${command}"
    fi
}

run_scp()
{
    if command -v sshpass >/dev/null 2>&1; then
        sshpass -p "${BOARD_PASSWORD}" scp "${SSH_OPTS[@]}" "$@"
    elif command -v expect >/dev/null 2>&1; then
        EXPECT_PASSWORD="${BOARD_PASSWORD}" EXPECT_ARGS="$*" \
        expect -c '
            set timeout -1
            set password $env(EXPECT_PASSWORD)
            eval spawn scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $env(EXPECT_ARGS)
            expect {
                -re "(?i)password:" {
                    send "$password\r"
                    exp_continue
                }
                eof
            }
            catch wait result
            exit [lindex $result 3]
        '
    else
        scp "${SSH_OPTS[@]}" "$@"
    fi
}

echo "Deploying CameraSubsystem web debug stack to ${BOARD_USER}@${BOARD_HOST}:${REMOTE_ROOT}"

if [[ "${STOP_BEFORE_DEPLOY}" == "1" ]]; then
    run_ssh "if [ -x '${remote_scripts}/rk3576-board-debug-stack.sh' ]; then '${remote_scripts}/rk3576-board-debug-stack.sh' stop || true; fi"
fi

run_ssh \
    "mkdir -p '${remote_bin}' '${remote_web}' '${remote_scripts}' '${remote_logs}' '${remote_run}' '${remote_tmp}' '${remote_recordings}' '${remote_models}'"
run_ssh "rm -rf '${remote_web}'/*"

required_bins=(
    "${BIN_DIR}/camera_publisher_example"
    "${BIN_DIR}/camera_codec_server"
    "${BIN_DIR}/camera_detection_server"
    "${BIN_DIR}/camera_detection_control_client_example"
    "${BIN_DIR}/camera_detection_result_client_example"
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

for asset in "${LOCAL_DETECTION_MODEL}" "${LOCAL_DETECTION_LABELS}"; do
    if [[ ! -f "${asset}" ]]; then
        echo "missing detection asset: ${asset}"
        exit 1
    fi
done

run_scp \
    "${BIN_DIR}/camera_publisher_example" \
    "${BIN_DIR}/camera_codec_server" \
    "${BIN_DIR}/camera_detection_server" \
    "${BIN_DIR}/camera_detection_control_client_example" \
    "${BIN_DIR}/camera_detection_result_client_example" \
    "${GATEWAY_BIN}" \
    "${TARGET}:${remote_bin}/"

optional_bins=(
    "${BIN_DIR}/mplane_dmabuf_probe"
    "${BIN_DIR}/dmabuf_smoke_test"
)

for bin in "${optional_bins[@]}"; do
    if [[ -f "${bin}" ]]; then
        run_scp "${bin}" "${TARGET}:${remote_bin}/"
    fi
done

run_scp \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-v1-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-multi-session-control-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-stability-test-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/codec_server/scripts/codec-mp4-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/web_preview/scripts/web-record-freeze-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/extensions/web_preview/scripts/web-codec-restart-smoke-rk3576.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-failover-smoke.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-lifecycle-smoke.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-dataplane-v2-slow-consumer-smoke.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-board-smoke-suite.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-mplane-readiness-probe.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-multi-camera-topology-smoke.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-board-debug-stack.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-run-web-stack.sh" \
    "${PROJECT_ROOT}/scripts/rk3576-detection-p0-smoke.sh" \
    "${TARGET}:${remote_scripts}/"

run_scp -r "${WEB_DIST}/"* "${TARGET}:${remote_web}/"
run_scp \
    "${LOCAL_DETECTION_MODEL}" \
    "${LOCAL_DETECTION_LABELS}" \
    "${TARGET}:${remote_models}/"

run_ssh \
    "chmod +x '${remote_bin}/camera_publisher_example' '${remote_bin}/camera_codec_server' '${remote_bin}/camera_detection_server' '${remote_bin}/camera_detection_control_client_example' '${remote_bin}/camera_detection_result_client_example' '${remote_bin}/web_preview_gateway' '${remote_scripts}'/*.sh; [ ! -f '${remote_bin}/mplane_dmabuf_probe' ] || chmod +x '${remote_bin}/mplane_dmabuf_probe'"

echo "Deploy complete."
echo "Board root: ${REMOTE_ROOT}"
echo "Web URL: http://${BOARD_HOST}:8080"
