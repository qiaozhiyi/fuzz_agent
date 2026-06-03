#!/usr/bin/env python3
import argparse
import json
from collections import defaultdict
from pathlib import Path


LLM_MODES = {
    "full-agent",
    "ai-direct",
    "no-static-analysis",
    "no-semantic-context",
    "single-agent-coordinator",
    "single-agent-dictionary",
    "no-mutator",
}


def load_manifest(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(f"PyYAML is required: {exc}")
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def stage_modes(manifest: dict, stage_name: str) -> list[str]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    return list(stage.get("modes") or manifest.get("modes") or [])


def expected_run_rows(manifest: dict, stage_name: str) -> list[dict[str, object]]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    repeats = int(stage.get("repeats") or 0)
    targets = manifest.get("targets") or []
    modes = stage_modes(manifest, stage_name)
    expected = []
    for repeat in range(1, repeats + 1):
        for target in targets:
            target_id = target.get("id", "")
            for mode in modes:
                expected.append({
                    "run_id": f"arch_{stage_name.lower()}_{target_id}_{mode}_r{repeat:02d}",
                    "target": target_id,
                    "mode": mode,
                    "repeat": repeat,
                })
    return expected


def expected_run_ids(manifest: dict, stage_name: str) -> list[str]:
    return [str(row["run_id"]) for row in expected_run_rows(manifest, stage_name)]


def stage_estimate(manifest: dict, stage_name: str) -> dict[str, float | int]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    repeats = int(stage.get("repeats") or 0)
    budget_sec = int(stage.get("budget_sec") or 0)
    targets = manifest.get("targets") or []
    modes = stage_modes(manifest, stage_name)
    defaults = manifest.get("defaults") or {}
    non_llm_parallel = max(1, int(defaults.get("non_llm_parallel") or 1))
    llm_parallel = max(1, int(defaults.get("llm_parallel") or 1))
    llm_cells = sum(1 for mode in modes if mode in LLM_MODES) * len(targets) * repeats
    non_llm_cells = (len(modes) * len(targets) * repeats) - llm_cells
    llm_wall_sec = (llm_cells * budget_sec) / llm_parallel
    non_llm_wall_sec = (non_llm_cells * budget_sec) / non_llm_parallel
    return {
        "repeats": repeats,
        "budget_sec": budget_sec,
        "targets": len(targets),
        "modes": len(modes),
        "llm_cells": llm_cells,
        "non_llm_cells": non_llm_cells,
        "total_cells": llm_cells + non_llm_cells,
        "core_hours": (llm_cells + non_llm_cells) * budget_sec / 3600.0,
        "conservative_wall_hours": (llm_wall_sec + non_llm_wall_sec) / 3600.0,
        "llm_wall_hours": llm_wall_sec / 3600.0,
        "non_llm_wall_hours": non_llm_wall_sec / 3600.0,
        "llm_parallel": llm_parallel,
        "non_llm_parallel": non_llm_parallel,
    }


def remaining_estimate(manifest: dict,
                       stage_name: str,
                       rows: list[dict[str, object]]) -> dict[str, float | int]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    defaults = manifest.get("defaults") or {}
    budget_sec = int(stage.get("budget_sec") or 0)
    non_llm_parallel = max(1, int(defaults.get("non_llm_parallel") or 1))
    llm_parallel = max(1, int(defaults.get("llm_parallel") or 1))
    remaining = [
        row for row in rows
        if not (row.get("status") == "completed" and str(row.get("exit_code", "")) in {"", "0"})
    ]
    llm_cells = sum(1 for row in remaining if row.get("mode") in LLM_MODES)
    non_llm_cells = len(remaining) - llm_cells
    llm_wall_sec = (llm_cells * budget_sec) / llm_parallel
    non_llm_wall_sec = (non_llm_cells * budget_sec) / non_llm_parallel
    return {
        "remaining_cells": len(remaining),
        "remaining_llm_cells": llm_cells,
        "remaining_non_llm_cells": non_llm_cells,
        "remaining_core_hours": len(remaining) * budget_sec / 3600.0,
        "remaining_conservative_wall_hours": (llm_wall_sec + non_llm_wall_sec) / 3600.0,
        "remaining_llm_wall_hours": llm_wall_sec / 3600.0,
        "remaining_non_llm_wall_hours": non_llm_wall_sec / 3600.0,
    }


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def latest_json(paths: list[Path]) -> dict:
    if not paths:
        return {}
    newest = max(paths, key=lambda path: path.stat().st_mtime)
    try:
        data = json.loads(newest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        data = {}
    data["_path"] = str(newest)
    return data


def read_json_path(path: str) -> dict:
    if not path:
        return {}
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Show architecture pilot stage progress.")
    parser.add_argument("stage", nargs="?", default="A", help="Stage name, e.g. A, B, or C.")
    parser.add_argument(
        "--root",
        default="results/fuzzpilot_architecture_pilot",
        help="Architecture pilot result root.",
    )
    parser.add_argument(
        "--manifest",
        default="experiments/manifests/paper_architecture_pilot.yaml",
        help="Architecture pilot manifest.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of text.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root)
    run_root = root / "runs"
    log_root = root / "logs"
    manifest = load_manifest(Path(args.manifest))
    expected_rows = expected_run_rows(manifest, args.stage)
    expected = [str(row["run_id"]) for row in expected_rows]
    estimate = stage_estimate(manifest, args.stage)

    rows = []
    status_counts: dict[str, int] = defaultdict(int)
    missing = []
    for expected_row in expected_rows:
        run_id = str(expected_row["run_id"])
        run_dir = run_root / run_id
        status = read_text(run_dir / "status") if run_dir.exists() else ""
        exit_code = read_text(run_dir / "exit_code") if run_dir.exists() else ""
        if not status:
            missing.append(run_id)
            status = "missing"
        status_counts[status] += 1
        rows.append({
            "run_id": run_id,
            "target": expected_row["target"],
            "mode": expected_row["mode"],
            "repeat": expected_row["repeat"],
            "status": status,
            "exit_code": exit_code,
        })

    latest_status = latest_json(sorted(log_root.glob(f"stage_{args.stage.lower()}_*_status.json")))
    failed = [
        row for row in rows
        if row["status"] in {"failed", "skipped-missing-api-key"} or
        (row["exit_code"] and row["exit_code"] != "0")
    ]
    running = [row for row in rows if row["status"] == "running"]
    status_logs = latest_status.get("logs") if isinstance(latest_status.get("logs"), dict) else {}
    noise_summary = read_json_path(str(status_logs.get("noise_summary", "")))
    if noise_summary:
        noise_summary["_path"] = str(status_logs.get("noise_summary", ""))
    stage_plan = read_json_path(str(latest_status.get("stage_plan") or status_logs.get("stage_plan", "")))
    if stage_plan:
        stage_plan["_path"] = str(latest_status.get("stage_plan") or status_logs.get("stage_plan", ""))

    payload = {
        "stage": args.stage,
        "root": str(root),
        "expected_cells": len(expected),
        "discovered_cells": len(expected) - len(missing),
        "status_counts": dict(sorted(status_counts.items())),
        "running": running,
        "failed_or_skipped": failed,
        "missing": missing,
        "latest_stage_status": latest_status,
        "stage_plan": stage_plan,
        "runtime_noise_summary": noise_summary,
        "estimate": estimate,
        "remaining_estimate": remaining_estimate(manifest, args.stage, rows),
    }

    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0

    print(f"Architecture pilot stage {args.stage}")
    print(f"root: {root}")
    print(f"cells: {payload['discovered_cells']}/{payload['expected_cells']} discovered")
    print(f"statuses: {payload['status_counts']}")
    print(
        "estimate: {total} cells ({llm} LLM-bearing, {non_llm} non-LLM), "
        "{core:.1f} core-hours, conservative wall-clock {wall:.1f} h".format(
            total=estimate["total_cells"],
            llm=estimate["llm_cells"],
            non_llm=estimate["non_llm_cells"],
            core=estimate["core_hours"],
            wall=estimate["conservative_wall_hours"],
        )
    )
    print(
        "schedule model: LLM {llm_wall:.1f} h at parallel={llm_parallel}; "
        "non-LLM {non_llm_wall:.1f} h at parallel={non_llm_parallel}".format(
            llm_wall=estimate["llm_wall_hours"],
            llm_parallel=estimate["llm_parallel"],
            non_llm_wall=estimate["non_llm_wall_hours"],
            non_llm_parallel=estimate["non_llm_parallel"],
        )
    )
    remaining = payload["remaining_estimate"]
    print(
        "remaining: {cells} cells ({llm} LLM-bearing, {non_llm} non-LLM), "
        "{core:.1f} core-hours, conservative wall-clock {wall:.1f} h".format(
            cells=remaining["remaining_cells"],
            llm=remaining["remaining_llm_cells"],
            non_llm=remaining["remaining_non_llm_cells"],
            core=remaining["remaining_core_hours"],
            wall=remaining["remaining_conservative_wall_hours"],
        )
    )
    if latest_status:
        print(f"latest wrapper status: {latest_status.get('_path', '')}")
        print(
            "exit codes: preflight={preflight} runner={runner} summary={summary}".format(
                preflight=latest_status.get("preflight_exit_code", ""),
                runner=latest_status.get("runner_exit_code", ""),
                summary=latest_status.get("summary_exit_code", ""),
            )
        )
        if latest_status.get("report"):
            print(f"report: {latest_status['report']}")
    if stage_plan:
        resource_plan = stage_plan.get("resource_plan") if isinstance(stage_plan.get("resource_plan"), dict) else {}
        print(f"stage plan: {stage_plan.get('_path', '')}")
        print(
            "planned: {cells} cells, llm_parallel={llm_parallel}, "
            "non_llm_parallel={non_llm_parallel}, fuzz_slots={fuzz_slots}".format(
                cells=resource_plan.get("total_cells", ""),
                llm_parallel=resource_plan.get("llm_parallel", ""),
                non_llm_parallel=resource_plan.get("non_llm_parallel", ""),
                fuzz_slots=resource_plan.get("fuzz_slots", ""),
            )
        )
    if noise_summary:
        print(f"runtime noise summary: {noise_summary.get('_path', '')}")
        print(
            "runtime noise: samples={samples} max_load1_per_cpu={load:.3f} "
            "min_mem_available_kib={mem} max_swap_used_kib={swap}".format(
                samples=noise_summary.get("samples", 0),
                load=float(noise_summary.get("max_load1_per_cpu", 0.0)),
                mem=noise_summary.get("min_mem_available_kib", ""),
                swap=noise_summary.get("max_swap_used_kib", ""),
            )
        )
        warnings = noise_summary.get("warnings") or []
        if warnings:
            print("runtime noise warnings:")
            for warning in warnings[:12]:
                print(f"  {warning}")
    if running:
        print("running:")
        for row in running[:12]:
            print(f"  {row['run_id']}")
    if failed:
        print("failed/skipped:")
        for row in failed[:12]:
            print(f"  {row['run_id']} status={row['status']} exit={row['exit_code']}")
    if missing:
        print("missing:")
        for run_id in missing[:12]:
            print(f"  {run_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
