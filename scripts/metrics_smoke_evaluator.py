#!/usr/bin/env python3
"""
metrics_smoke_evaluator.py — Metrics 阈值判定核心
用法:
    python3 metrics_smoke_evaluator.py \
        --history PATH \
        [--snapshot PATH] \
        [--tier quick|full|extended] \
        [--output-report PATH]

退出码:
    0 — PASS
    1 — FAIL（至少一项检查未通过）
    2 — ERROR（输入缺失、全部 malformed、参数错误等）
"""

import argparse
import json
import os
import sys
import time
from typing import Any, Dict, List, Optional


def get_env_int(name: str, default: int) -> int:
    val = os.environ.get(name)
    if val is None:
        return default
    try:
        return int(val)
    except ValueError:
        return default


def get_env_float(name: str, default: float) -> float:
    val = os.environ.get(name)
    if val is None:
        return default
    try:
        return float(val)
    except ValueError:
        return default


class CheckResult:
    def __init__(self, check_id: str, category: str, result: str,
                 actual: Any = None, threshold: Any = None, op: str = "",
                 reason: str = ""):
        self.check_id = check_id
        self.category = category
        self.result = result  # PASS / FAIL / SKIP
        self.actual = actual
        self.threshold = threshold
        self.op = op
        self.reason = reason

    def to_dict(self) -> dict:
        d = {
            "check_id": self.check_id,
            "category": self.category,
            "result": self.result,
        }
        if self.actual is not None:
            d["actual"] = self.actual
        if self.threshold is not None:
            d["threshold"] = self.threshold
        if self.op:
            d["op"] = self.op
        if self.reason:
            d["reason"] = self.reason
        return d


def print_report_line(check: CheckResult) -> None:
    if check.result == "SKIP":
        print(f"  [SKIP] {check.check_id} ({check.reason})")
    elif check.result == "PASS":
        print(f"  [PASS] {check.check_id} ({check.actual} {check.op} {check.threshold})")
    else:
        print(f"  [FAIL] {check.check_id} ({check.actual} {check.op} {check.threshold}) category={check.category}")


def max_check(check_id: str, category: str, actual: int, threshold: int) -> CheckResult:
    if threshold < 0:
        return CheckResult(check_id, category, "SKIP", actual, threshold, "<=", "not capped")
    return CheckResult(
        check_id, category,
        "PASS" if actual <= threshold else "FAIL",
        actual, threshold, "<=")


def evaluate_stream_checks(tier: str, stream: dict, duration_sec: int,
                           requested_fps: int, subscriber_count: int) -> List[CheckResult]:
    results: List[CheckResult] = []

    capture = stream.get("capture_frame_count", 0)
    dropped = stream.get("capture_dropped_count", 0)
    v2_sent = stream.get("v2_sent_frame_count", 0)
    v2_fail = stream.get("v2_send_failure_count", 0)
    lease_exhausted = stream.get("lease_exhausted_count", 0)
    active_leases = stream.get("active_lease_count", 0)
    release_pending = stream.get("release_pending_count", 0)
    release_timeout = stream.get("release_timeout_count", 0)
    disconnections = stream.get("disconnection_count", 0)
    degraded = stream.get("source_degraded", False)
    cur_state = stream.get("current_state", 0)
    # 命令行/环境变量显式指定的 fps 优先于 stream 自报值。
    # RK3576 USB 摄像头场景中，驱动标称 fps 可能高于实际稳定帧率。
    req_fps = requested_fps if requested_fps > 0 else stream.get("source_requested_fps", 30)
    cur_fps = stream.get("source_current_target_fps", req_fps)
    degradation_count = stream.get("source_degradation_count", 0)
    degradation_recovery = stream.get("source_degradation_recovery_count", 0)

    min_frames = get_env_int("METRICS_MIN_CAPTURE_FRAMES", 0)
    if min_frames == 0:
        ratio = 0.5 if tier == "quick" else 0.7
        min_frames = max(1, int(duration_sec * req_fps * ratio))

    max_dropped = get_env_int("METRICS_MAX_CAPTURE_DROPPED", 0)
    max_v2_fail = get_env_int("METRICS_MAX_V2_SEND_FAILURE", 0)
    max_release_pending = get_env_int("METRICS_MAX_RELEASE_PENDING",
                                       subscriber_count if tier != "quick" else subscriber_count * 2)
    max_release_timeout = get_env_int("METRICS_MAX_RELEASE_TIMEOUT", 0)
    max_active_leases = get_env_int("METRICS_MAX_ACTIVE_LEASES", subscriber_count * 2)
    max_lease_exhausted = get_env_int("METRICS_MAX_LEASE_EXHAUSTED", 0)
    max_disconnection = get_env_int("METRICS_MAX_DISCONNECTION", 0)
    allow_degraded = get_env_int("METRICS_ALLOW_DEGRADED", 0)
    expected_state = get_env_int("METRICS_EXPECTED_STATE", 2)

    v2_min = get_env_int("METRICS_MIN_V2_SENT", 0)
    if v2_min == 0:
        v2_ratio = 0.5 if tier == "quick" else 0.7
        v2_min = max(1, int(duration_sec * req_fps * v2_ratio * subscriber_count))

    # 采集帧数下限。
    results.append(CheckResult(
        "capture_min_frames", "METRICS_CAPTURE_FAIL",
        "PASS" if capture >= min_frames else "FAIL",
        capture, min_frames, ">="))

    # 采集层丢帧上限。
    results.append(max_check(
        "capture_zero_dropped", "METRICS_CAPTURE_FAIL", dropped, max_dropped))

    # DataPlaneV2 发送帧数下限。
    results.append(CheckResult(
        "v2_min_sent", "METRICS_DATAPLANE_FAIL",
        "PASS" if v2_sent >= v2_min else "FAIL",
        v2_sent, v2_min, ">="))

    # DataPlaneV2 发送失败上限。
    results.append(CheckResult(
        "v2_zero_failures", "METRICS_DATAPLANE_FAIL",
        "PASS" if v2_fail <= max_v2_fail else "FAIL",
        v2_fail, max_v2_fail, "<="))

    # 非故障场景不应发生断连。
    results.append(CheckResult(
        "disconnection_count", "METRICS_CAPTURE_FAIL",
        "PASS" if disconnections <= max_disconnection else "FAIL",
        disconnections, max_disconnection, "<="))

    # 默认 smoke 不应触发降级。
    results.append(CheckResult(
        "source_not_degraded", "METRICS_DEGRADATION_FAIL",
        "PASS" if (not degraded or allow_degraded) else "FAIL",
        degraded, False, "==",
        reason="degraded allowed" if allow_degraded else ""))

    # DMA-BUF lease 不应耗尽。
    results.append(max_check(
        "lease_exhausted", "METRICS_DATAPLANE_FAIL", lease_exhausted, max_lease_exhausted))

    # 活跃 lease 数应在订阅者规模内收敛。
    results.append(CheckResult(
        "active_leases_max", "METRICS_DATAPLANE_FAIL",
        "PASS" if active_leases <= max_active_leases else "FAIL",
        active_leases, max_active_leases, "<="))

    # ReleaseFrame pending 数应收敛。
    results.append(CheckResult(
        "release_pending_max", "METRICS_DATAPLANE_FAIL",
        "PASS" if release_pending <= max_release_pending else "FAIL",
        release_pending, max_release_pending, "<="))

    # ReleaseFrame 不应超时回收。
    results.append(max_check(
        "release_timeout", "METRICS_DATAPLANE_FAIL", release_timeout, max_release_timeout))

    # CameraSource 应处于 Streaming 状态。
    results.append(CheckResult(
        "expected_state", "METRICS_CAPTURE_FAIL",
        "PASS" if cur_state == expected_state else "FAIL",
        cur_state, expected_state, "=="))

    # 检查 source 当前目标 fps 未明显低于请求 fps，避免误把降级态当作正常态。
    if tier != "quick":
        results.append(CheckResult(
            "source_target_fps_not_degraded", "METRICS_DEGRADATION_FAIL",
            "PASS" if cur_fps >= req_fps * 0.8 else "FAIL",
            cur_fps, int(req_fps * 0.8), ">="))

    # extended 档验证降级后可恢复。
    if tier == "extended":
        if degradation_count > 0:
            results.append(CheckResult(
                "degradation_recovery", "METRICS_DEGRADATION_FAIL",
                "PASS" if (degradation_recovery > 0 and not degraded) else "FAIL",
                f"count={degradation_count},recovery={degradation_recovery},degraded={degraded}",
                "recovery>0 and degraded=false", "=="))

    return results


def evaluate_global_checks(tier: str, global_data: dict, duration_sec: int,
                           requested_fps: int, subscriber_count: int) -> List[CheckResult]:
    results: List[CheckResult] = []
    # Phase 1 单 stream 中，global 检查镜像 stream 级 DataPlaneV2/release 检查。
    v2_sent = global_data.get("v2_sent_frame_count", 0)
    v2_fail = global_data.get("v2_send_failure_count", 0)
    release_pending = global_data.get("release_pending_count", 0)
    release_timeout = global_data.get("release_timeout_count", 0)

    v2_min = get_env_int("METRICS_MIN_V2_SENT", 0)
    if v2_min == 0:
        v2_ratio = 0.5 if tier == "quick" else 0.7
        v2_min = max(1, int(duration_sec * requested_fps * v2_ratio * subscriber_count))

    max_v2_fail = get_env_int("METRICS_MAX_V2_SEND_FAILURE", 0)
    max_release_pending = get_env_int("METRICS_MAX_RELEASE_PENDING",
                                       subscriber_count if tier != "quick" else subscriber_count * 2)
    max_release_timeout = get_env_int("METRICS_MAX_RELEASE_TIMEOUT", 0)

    results.append(CheckResult(
        "global_v2_min_sent", "METRICS_DATAPLANE_FAIL",
        "PASS" if v2_sent >= v2_min else "FAIL",
        v2_sent, v2_min, ">="))

    results.append(CheckResult(
        "global_v2_zero_failures", "METRICS_DATAPLANE_FAIL",
        "PASS" if v2_fail <= max_v2_fail else "FAIL",
        v2_fail, max_v2_fail, "<="))

    results.append(CheckResult(
        "global_release_pending_max", "METRICS_DATAPLANE_FAIL",
        "PASS" if release_pending <= max_release_pending else "FAIL",
        release_pending, max_release_pending, "<="))

    results.append(max_check(
        "global_release_timeout", "METRICS_DATAPLANE_FAIL", release_timeout, max_release_timeout))

    return results


def evaluate_trend_checks(tier: str, samples: List[dict]) -> List[CheckResult]:
    results: List[CheckResult] = []
    if tier != "extended" or len(samples) < 2:
        return results

    # capture_frame_count 在 extended 档中应持续增长。
    captures = [s.get("streams", [{}])[0].get("capture_frame_count", 0) for s in samples]
    monotonic = all(captures[i] < captures[i+1] for i in range(len(captures)-1))
    results.append(CheckResult(
        "capture_monotonic", "METRICS_TREND_FAIL",
        "PASS" if monotonic else "FAIL",
        "monotonic" if monotonic else "stalled", True, "=="))

    # Phase 1 publisher example 尚未接入 FrameBroker，队列深度趋势暂跳过。
    results.append(CheckResult(
        "queue_depth_stable", "METRICS_BROKER_FAIL",
        "SKIP", reason="FrameBroker not wired in publisher example"))

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Metrics smoke evaluator")
    parser.add_argument("--history", help="Path to metrics_history.jsonl")
    parser.add_argument("--snapshot", help="Path to metrics_snapshot.json (fallback)")
    parser.add_argument("--tier", choices=["quick", "full", "extended"], default="full")
    parser.add_argument("--output-report", default="metrics_smoke_report.json")
    parser.add_argument("--device", default=os.environ.get("DEVICE", "/dev/video45"))
    parser.add_argument("--duration-sec", type=int, default=get_env_int("METRICS_DURATION_SEC", 60))
    parser.add_argument("--subscriber-count", type=int, default=get_env_int("METRICS_SUBSCRIBER_COUNT", 2))
    parser.add_argument("--requested-fps", type=int, default=get_env_int("METRICS_REQUESTED_FPS", 0))
    args = parser.parse_args()

    samples: List[dict] = []

    # 优先读取 history。
    if args.history and os.path.exists(args.history) and os.path.getsize(args.history) > 0:
        with open(args.history, "r", encoding="utf-8") as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    samples.append(obj)
                except json.JSONDecodeError as e:
                    print(f"warning: malformed JSON at line {line_num}: {e}", file=sys.stderr)

    # history 无有效样本时回退到 snapshot。
    if not samples and args.snapshot and os.path.exists(args.snapshot) and os.path.getsize(args.snapshot) > 0:
        try:
            with open(args.snapshot, "r", encoding="utf-8") as f:
                obj = json.loads(f.read())
                samples.append(obj)
        except json.JSONDecodeError as e:
            print(f"warning: malformed snapshot JSON: {e}", file=sys.stderr)

    if not samples:
        src_desc = "history" if args.history else ""
        if args.history and args.snapshot:
            src_desc = "history and snapshot"
        elif args.snapshot:
            src_desc = "snapshot"
        print(f"error: no valid metrics samples found ({src_desc} empty, missing, or all malformed)", file=sys.stderr)
        report = {
            "tier": args.tier,
            "duration_sec": args.duration_sec,
            "device": args.device,
            "result": "ERROR",
            "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "error": "no valid metrics samples",
        }
        with open(args.output_report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        return 2

    # 使用最后一条样本作为 global 阈值判定快照；退出期 final snapshot 可能已经没有
    # active stream provider，因此 stream 阈值使用最后一条非空 streams 样本。
    final_sample = samples[-1]
    final_stream_sample: Optional[dict] = None
    for sample in reversed(samples):
        if sample.get("streams", []):
            final_stream_sample = sample
            break

    streams = final_stream_sample.get("streams", []) if final_stream_sample else []
    global_data = final_sample.get("global", {})

    if not streams:
        print("error: no metrics sample contains streams", file=sys.stderr)
        report = {
            "tier": args.tier,
            "duration_sec": args.duration_sec,
            "device": args.device,
            "result": "ERROR",
            "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "error": "no streams in metrics samples",
        }
        with open(args.output_report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        return 2

    # 执行判定。
    stream_results: List[dict] = []
    all_checks: List[CheckResult] = []
    failed_categories: set = set()

    print(f"=== Stream Metrics Smoke Report ===")
    print(f"tier={args.tier} duration={args.duration_sec}s device={args.device} streams={len(streams)}")

    for stream in streams:
        stream_id = stream.get("stream_id", "unknown")
        print(f"\n[stream={stream_id}]")
        checks = evaluate_stream_checks(
            args.tier, stream, args.duration_sec, args.requested_fps, args.subscriber_count)

        # 输出可读报告。
        for c in checks:
            print_report_line(c)

        all_checks.extend(checks)
        stream_results.append({
            "stream_id": stream_id,
            "metrics_snapshot_final": stream,
            "checks": [c.to_dict() for c in checks],
        })

    # Global 检查。
    global_requested_fps = args.requested_fps
    if global_requested_fps <= 0 and streams:
        global_requested_fps = streams[0].get("source_requested_fps", 30)
    print("\n[global]")
    global_checks = evaluate_global_checks(
        args.tier, global_data, args.duration_sec, global_requested_fps, args.subscriber_count)
    for c in global_checks:
        print_report_line(c)
    all_checks.extend(global_checks)

    # extended 档趋势检查。
    if args.tier == "extended":
        print("\n[trend]")
        trend_checks = evaluate_trend_checks(args.tier, samples)
        for c in trend_checks:
            print_report_line(c)
        all_checks.extend(trend_checks)

    # 汇总结果。
    passed = sum(1 for c in all_checks if c.result == "PASS")
    failed = sum(1 for c in all_checks if c.result == "FAIL")
    skipped = sum(1 for c in all_checks if c.result == "SKIP")

    for c in all_checks:
        if c.result == "FAIL":
            failed_categories.add(c.category)

    result = "PASS" if failed == 0 else "FAIL"

    print(f"\nresult={result} checks_passed={passed}/{passed+failed} skipped={skipped} categories={list(failed_categories)}")

    report = {
        "tier": args.tier,
        "duration_sec": args.duration_sec,
        "device": args.device,
        "result": result,
        "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "streams": stream_results,
        "global_checks": [c.to_dict() for c in global_checks],
        "summary": {
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "failed_categories": sorted(list(failed_categories)),
        },
    }

    if args.tier == "extended":
        report["trend_checks"] = [c.to_dict() for c in trend_checks]

    with open(args.output_report, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)

    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
