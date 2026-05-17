#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD_HOST="${BOARD_HOST:-192.168.31.9}"
BOARD_USER="${BOARD_USER:-luckfox}"
BOARD_PASSWORD="${BOARD_PASSWORD:-luckfox}"
BOARD_DIR="${BOARD_DIR:-/tmp/camera_subsystem_device_discovery}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/logs/rk3576-device-discovery-scan}"

TARGET="${BOARD_USER}@${BOARD_HOST}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

mkdir -p "${LOCAL_LOG_DIR}"

raw_report="${LOCAL_LOG_DIR}/device_discovery.tsv"
json_report="${LOCAL_LOG_DIR}/device_discovery_report.json"

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
        local scp_args
        scp_args="$*"
        EXPECT_PASSWORD="${BOARD_PASSWORD}" EXPECT_SCP_ARGS="${scp_args}" \
        expect -c '
            set timeout -1
            set password $env(EXPECT_PASSWORD)
            set scp_args [split $env(EXPECT_SCP_ARGS)]
            spawn scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null {*}$scp_args
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

remote_scan='
sanitize()
{
    printf "%s" "$1" | tr "\t\r\n" "   "
}

find_parent_file()
{
    local path="$1"
    local name="$2"
    while [ -n "$path" ] && [ "$path" != "/" ]; do
        if [ -f "$path/$name" ]; then
            cat "$path/$name"
            return 0
        fi
        path=$(dirname "$path")
    done
    return 1
}

printf "device\tname\tdriver\tbus_info\tsubsystem\tvendor_id\tproduct_id\tserial\tphysical_path\n"
for node in /sys/class/video4linux/video*; do
    [ -e "$node" ] || continue
    video=$(basename "$node")
    device="/dev/$video"
    name=$(cat "$node/name" 2>/dev/null || true)
    sysdev=$(readlink -f "$node/device" 2>/dev/null || true)
    driver=""
    subsystem=""
    bus_info=""
    vendor=""
    product=""
    serial=""
    if [ -n "$sysdev" ]; then
        driver=$(basename "$(readlink -f "$sysdev/driver" 2>/dev/null)" 2>/dev/null || true)
        subsystem=$(basename "$(readlink -f "$sysdev/subsystem" 2>/dev/null)" 2>/dev/null || true)
        bus_info=$(basename "$sysdev")
        vendor=$(find_parent_file "$sysdev" idVendor 2>/dev/null || true)
        product=$(find_parent_file "$sysdev" idProduct 2>/dev/null || true)
        serial=$(find_parent_file "$sysdev" serial 2>/dev/null || true)
    fi
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
        "$(sanitize "$device")" \
        "$(sanitize "$name")" \
        "$(sanitize "$driver")" \
        "$(sanitize "$bus_info")" \
        "$(sanitize "$subsystem")" \
        "$(sanitize "$vendor")" \
        "$(sanitize "$product")" \
        "$(sanitize "$serial")" \
        "$(sanitize "$sysdev")"
done
'

remote_report="${BOARD_DIR}/device_discovery.tsv"
run_ssh "mkdir -p '${BOARD_DIR}'" >/dev/null
run_ssh "sh -lc $(printf '%q' "${remote_scan}") > '${remote_report}'" >/dev/null
run_scp "${TARGET}:${remote_report}" "${raw_report}" >/dev/null

python3 - "${raw_report}" "${json_report}" "${BOARD_USER}@${BOARD_HOST}" <<'PY'
import csv
import json
import sys
from datetime import datetime, timezone

raw_path, json_path, board = sys.argv[1:4]
with open(raw_path, newline="", encoding="utf-8") as f:
    rows = list(csv.DictReader(f, delimiter="\t"))

for row in rows:
    vendor = row.get("vendor_id", "")
    product = row.get("product_id", "")
    serial = row.get("serial", "")
    bus_info = row.get("bus_info", "")
    if vendor and product and serial:
        physical_id = f"usb:{vendor}:{product}:{serial}"
    elif vendor and product and bus_info:
        physical_id = f"usb:{vendor}:{product}:{bus_info}"
    elif bus_info:
        physical_id = f"{row.get('subsystem', 'unknown')}:{bus_info}"
    else:
        physical_id = "unknown"
    row["physical_id"] = physical_id

report = {
    "board": board,
    "timestamp_iso": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "device_count": len(rows),
    "devices": rows,
}

with open(json_path, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=2)
    f.write("\n")

print(f"device_discovery_scan_result=PASS device_count={len(rows)}")
print(f"raw_report={raw_path}")
print(f"json_report={json_path}")
PY
