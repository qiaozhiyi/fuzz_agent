#!/usr/bin/env python3
import argparse
import json
import os
import signal
import shutil
import time
from pathlib import Path


STOP = False


def handle_stop(_signum, _frame):
    global STOP
    STOP = True


def meminfo() -> dict[str, int]:
    values: dict[str, int] = {}
    path = Path("/proc/meminfo")
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1].isdigit():
            values[parts[0].rstrip(":")] = int(parts[1])
    return values


def proc_cmdlines() -> list[str]:
    commands: list[str] = []
    proc = Path("/proc")
    for child in proc.iterdir():
        if not child.name.isdigit():
            continue
        try:
            raw = (child / "cmdline").read_bytes()
        except OSError:
            continue
        if not raw:
            continue
        cmd = raw.replace(b"\x00", b" ").decode("utf-8", errors="replace").strip()
        if cmd:
            commands.append(cmd)
    return commands


def process_counts() -> dict[str, int]:
    counts = {
        "afl_fuzz": 0,
        "fuzzpilot_run": 0,
        "stage_wrapper": 0,
        "pilot_runner": 0,
        "target_fuzzer": 0,
    }
    for cmd in proc_cmdlines():
        if "afl-fuzz" in cmd:
            counts["afl_fuzz"] += 1
        if "fuzzpilot run" in cmd:
            counts["fuzzpilot_run"] += 1
        if "run_architecture_stage.sh" in cmd:
            counts["stage_wrapper"] += 1
        if "run_architecture_pilot.sh" in cmd:
            counts["pilot_runner"] += 1
        if any(name in cmd for name in ("cjson_fuzzer", "libpng_fuzzer", "libxml2_fuzzer")):
            counts["target_fuzzer"] += 1
    return counts


def disk_free_gib(path: Path) -> float:
    try:
        usage = shutil.disk_usage(path)
    except OSError:
        return 0.0
    return usage.free / (1024 ** 3)


def sample(stage: str, root: Path) -> dict[str, object]:
    mem = meminfo()
    load1, load5, load15 = os.getloadavg()
    cpu_count = os.cpu_count() or 1
    swap_total = int(mem.get("SwapTotal", 0))
    swap_free = int(mem.get("SwapFree", 0))
    return {
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "stage": stage,
        "cpu_count": cpu_count,
        "load1": load1,
        "load5": load5,
        "load15": load15,
        "load1_per_cpu": load1 / cpu_count,
        "mem_total_kib": int(mem.get("MemTotal", 0)),
        "mem_available_kib": int(mem.get("MemAvailable", 0)),
        "swap_total_kib": swap_total,
        "swap_used_kib": max(0, swap_total - swap_free),
        "disk_free_gib": disk_free_gib(root),
        "process_counts": process_counts(),
    }


def summarize(samples: list[dict[str, object]],
              max_load1_per_cpu_threshold: float,
              min_mem_available_kib: int,
              min_disk_free_gib: float) -> dict[str, object]:
    if not samples:
        return {
            "samples": 0,
            "thresholds": {
                "max_load1_per_cpu": max_load1_per_cpu_threshold,
                "min_mem_available_kib": min_mem_available_kib,
                "min_disk_free_gib": min_disk_free_gib,
            },
            "warnings": ["no runtime noise samples captured"],
        }
    max_load1 = max(float(item.get("load1", 0.0)) for item in samples)
    max_load1_per_cpu = max(float(item.get("load1_per_cpu", 0.0)) for item in samples)
    min_mem_available = min(int(item.get("mem_available_kib", 0)) for item in samples)
    max_swap_used = max(int(item.get("swap_used_kib", 0)) for item in samples)
    min_disk_free = min(float(item.get("disk_free_gib", 0.0)) for item in samples)
    max_counts: dict[str, int] = {}
    for item in samples:
        counts = item.get("process_counts") if isinstance(item.get("process_counts"), dict) else {}
        for name, value in counts.items():
            max_counts[name] = max(max_counts.get(name, 0), int(value))
    warnings = []
    if max_load1_per_cpu > max_load1_per_cpu_threshold:
        warnings.append(
            "runtime load exceeded threshold: "
            f"max_load1_per_cpu={max_load1_per_cpu:.3f} "
            f"threshold={max_load1_per_cpu_threshold:.3f}"
        )
    if max_swap_used > 0:
        warnings.append(f"swap used during stage: max_swap_used_kib={max_swap_used}")
    if min_mem_available < min_mem_available_kib:
        warnings.append(f"low memory availability observed: min_mem_available_kib={min_mem_available}")
    if min_disk_free < min_disk_free_gib:
        warnings.append(f"low disk availability observed: min_disk_free_gib={min_disk_free:.1f}")
    return {
        "samples": len(samples),
        "thresholds": {
            "max_load1_per_cpu": max_load1_per_cpu_threshold,
            "min_mem_available_kib": min_mem_available_kib,
            "min_disk_free_gib": min_disk_free_gib,
        },
        "started_at_utc": samples[0].get("timestamp_utc"),
        "finished_at_utc": samples[-1].get("timestamp_utc"),
        "max_load1": max_load1,
        "max_load1_per_cpu": max_load1_per_cpu,
        "min_mem_available_kib": min_mem_available,
        "max_swap_used_kib": max_swap_used,
        "min_disk_free_gib": min_disk_free,
        "max_process_counts": max_counts,
        "warnings": warnings,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Record low-overhead runtime noise samples for architecture stages.")
    parser.add_argument("--stage", default="A", help="Stage name.")
    parser.add_argument("--jsonl", required=True, help="Output JSONL sample path.")
    parser.add_argument("--summary", required=True, help="Output JSON summary path.")
    parser.add_argument("--interval-sec", type=float, default=30.0, help="Sampling interval in seconds.")
    parser.add_argument("--root", default=".", help="Filesystem path for disk-free sampling.")
    parser.add_argument(
        "--max-load1-per-cpu",
        type=float,
        default=1.25,
        help="Warn when max 1-minute load per CPU exceeds this value.",
    )
    parser.add_argument(
        "--min-mem-available-kib",
        type=int,
        default=1048576,
        help="Warn when MemAvailable drops below this value.",
    )
    parser.add_argument(
        "--min-disk-free-gib",
        type=float,
        default=20.0,
        help="Warn when free disk under --root drops below this value.",
    )
    parser.add_argument("--once", action="store_true", help="Take one sample and exit.")
    return parser.parse_args()


def main() -> int:
    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    args = parse_args()
    jsonl_path = Path(args.jsonl)
    summary_path = Path(args.summary)
    jsonl_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    root = Path(args.root)
    samples: list[dict[str, object]] = []

    with jsonl_path.open("a", encoding="utf-8") as out:
        while True:
            item = sample(args.stage, root)
            samples.append(item)
            out.write(json.dumps(item, sort_keys=True) + "\n")
            out.flush()
            if args.once or STOP:
                break
            time.sleep(max(1.0, args.interval_sec))

    summary_path.write_text(
        json.dumps(
            summarize(
                samples,
                args.max_load1_per_cpu,
                args.min_mem_available_kib,
                args.min_disk_free_gib,
            ),
            indent=2,
            sort_keys=True,
        ) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
