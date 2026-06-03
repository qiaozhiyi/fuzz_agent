#!/usr/bin/env python3
import argparse
import json
import shutil
import tarfile
import tempfile
from pathlib import Path


RUN_EVIDENCE_FILES = [
    "status",
    "exit_code",
    "runner_metadata.json",
    "coverage.csv",
    "events.jsonl",
    "agent_decisions.jsonl",
    "report.md",
]


def load_manifest(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(f"PyYAML is required: {exc}")
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def stage_modes(manifest: dict, stage_name: str) -> list[str]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    return list(stage.get("modes") or manifest.get("modes") or [])


def expected_run_ids(manifest: dict, stage_name: str) -> list[str]:
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    repeats = int(stage.get("repeats") or 0)
    targets = manifest.get("targets") or []
    modes = stage_modes(manifest, stage_name)
    run_ids = []
    for repeat in range(1, repeats + 1):
        for target in targets:
            target_id = target.get("id", "")
            for mode in modes:
                run_ids.append(f"arch_{stage_name.lower()}_{target_id}_{mode}_r{repeat:02d}")
    return run_ids


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def latest_status_json(log_root: Path, stage_name: str) -> Path | None:
    paths = sorted(log_root.glob(f"stage_{stage_name.lower()}_*_status.json"))
    if not paths:
        return None
    return max(paths, key=lambda path: path.stat().st_mtime)


def clean_wrapper_status(status: dict) -> bool:
    return (
        int(status.get("preflight_exit_code", 1)) == 0
        and int(status.get("runner_exit_code", 1)) == 0
        and int(status.get("summary_exit_code", 1)) == 0
    )


def copy_file(src: Path, dst_root: Path, rel: Path, copied: list[str]) -> None:
    dst = dst_root / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    copied.append(str(rel))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate and package a FuzzPilot architecture pilot paper bundle."
    )
    parser.add_argument("stage", help="Stage name, e.g. B or C.")
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
        "--out",
        default="",
        help="Output tar.gz path. Defaults to <root>/bundles/stage_<stage>_paper_bundle.tar.gz.",
    )
    parser.add_argument(
        "--allow-failed-gates",
        action="store_true",
        help="Package even if the acceptance report contains failed gates.",
    )
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help="Package even if expected cells are missing or incomplete.",
    )
    parser.add_argument(
        "--allow-noise-warnings",
        action="store_true",
        help="Package even if the runtime noise summary contains warnings.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stage = args.stage.upper()
    root = Path(args.root)
    run_root = root / "runs"
    log_root = root / "logs"
    manifest_path = Path(args.manifest)
    manifest = load_manifest(manifest_path)
    expected = expected_run_ids(manifest, stage)
    if not expected:
        raise SystemExit(f"unknown or empty stage: {stage}")

    out_path = Path(args.out) if args.out else root / "bundles" / f"stage_{stage.lower()}_paper_bundle.tar.gz"
    report_path = root / f"stage_{stage.lower()}_acceptance.md"
    csv_path = root / f"stage_{stage.lower()}_summary.csv"
    status_path = latest_status_json(log_root, stage)
    frozen_inputs_path = root / "frozen_inputs" / f"stage_{stage.lower()}_inputs.json"

    errors: list[str] = []
    warnings: list[str] = []
    copied: list[str] = []
    run_manifest: list[dict[str, object]] = []

    if not report_path.is_file() or report_path.stat().st_size == 0:
        errors.append(f"missing acceptance report: {report_path}")
    if not csv_path.is_file() or csv_path.stat().st_size == 0:
        errors.append(f"missing summary CSV: {csv_path}")
    if status_path is None:
        errors.append(f"missing wrapper status JSON for stage {stage}")

    report_text = report_path.read_text(encoding="utf-8") if report_path.exists() else ""
    if "- [FAIL]" in report_text and not args.allow_failed_gates:
        errors.append(f"acceptance report has failed gates: {report_path}")
    if stage == "S":
        warnings.append("Stage S is infrastructure only and must not be cited as paper evidence.")
    if stage == "A":
        warnings.append("Stage A is smoke evidence only; use Stage B/C for effectiveness claims.")
    if stage in {"B", "C"} and not frozen_inputs_path.is_file():
        errors.append(f"missing frozen Stage inputs: {frozen_inputs_path}")

    status = read_json(status_path) if status_path else {}
    if status and not clean_wrapper_status(status):
        errors.append(f"wrapper status is not clean: {status_path}")
    logs = status.get("logs") if isinstance(status.get("logs"), dict) else {}
    stage_plan_path = Path(str(status.get("stage_plan", "") or logs.get("stage_plan", "")))
    stage_plan = read_json(stage_plan_path) if str(stage_plan_path) else {}
    if not str(stage_plan_path) or not stage_plan_path.is_file():
        errors.append("missing stage plan in wrapper status")
    elif not stage_plan:
        errors.append(f"stage plan is unreadable: {stage_plan_path}")
    noise_summary_path = Path(str(logs.get("noise_summary", ""))) if logs.get("noise_summary") else None
    noise_summary = read_json(noise_summary_path) if noise_summary_path else {}
    if not noise_summary_path or not noise_summary_path.is_file():
        errors.append("missing runtime noise summary in wrapper status logs")
    elif int(noise_summary.get("samples") or 0) <= 0:
        errors.append(f"runtime noise summary has no samples: {noise_summary_path}")
    elif noise_summary.get("warnings") and not args.allow_noise_warnings:
        errors.append(
            "runtime noise summary has warnings: "
            + ", ".join(str(item) for item in noise_summary.get("warnings", [])[:6])
        )

    if not args.allow_incomplete:
        for run_id in expected:
            run_dir = run_root / run_id
            status_file = run_dir / "status"
            exit_file = run_dir / "exit_code"
            metadata_file = run_dir / "runner_metadata.json"
            run_status = status_file.read_text(encoding="utf-8").strip() if status_file.exists() else ""
            exit_code = exit_file.read_text(encoding="utf-8").strip() if exit_file.exists() else ""
            run_manifest.append({
                "run_id": run_id,
                "status": run_status or "missing",
                "exit_code": exit_code,
                "runner_metadata": str(metadata_file),
            })
            if not run_dir.is_dir():
                errors.append(f"missing run directory: {run_dir}")
                continue
            if run_status != "completed":
                errors.append(f"{run_id}: status is {run_status or 'missing'}, expected completed")
            if exit_code not in {"", "0"}:
                errors.append(f"{run_id}: exit_code is {exit_code}, expected 0")
            if not metadata_file.is_file() or metadata_file.stat().st_size == 0:
                errors.append(f"{run_id}: missing runner_metadata.json")
    else:
        for run_id in expected:
            run_dir = run_root / run_id
            status_file = run_dir / "status"
            exit_file = run_dir / "exit_code"
            run_manifest.append({
                "run_id": run_id,
                "status": status_file.read_text(encoding="utf-8").strip() if status_file.exists() else "missing",
                "exit_code": exit_file.read_text(encoding="utf-8").strip() if exit_file.exists() else "",
            })

    if errors:
        print("bundle check failed:")
        for error in errors[:40]:
            print(f"- {error}")
        if len(errors) > 40:
            print(f"- ... {len(errors) - 40} more")
        return 2

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f"fuzzpilot_stage_{stage.lower()}_bundle_") as tmp:
        tmp_root = Path(tmp) / f"fuzzpilot_stage_{stage.lower()}_paper_bundle"
        tmp_root.mkdir(parents=True)

        copy_file(manifest_path, tmp_root, Path("manifest/paper_architecture_pilot.yaml"), copied)
        copy_file(report_path, tmp_root, Path(f"stage/stage_{stage.lower()}_acceptance.md"), copied)
        copy_file(csv_path, tmp_root, Path(f"stage/stage_{stage.lower()}_summary.csv"), copied)
        if frozen_inputs_path.is_file():
            copy_file(frozen_inputs_path, tmp_root, Path(f"stage/stage_{stage.lower()}_inputs.json"), copied)
        if status_path:
            copy_file(status_path, tmp_root, Path("stage/wrapper_status.json"), copied)

        for log_name, log_path in (status.get("logs") or {}).items():
            path = Path(str(log_path))
            if path.is_file():
                suffix = path.suffix or ".log"
                copy_file(path, tmp_root, Path("logs") / f"{log_name}{suffix}", copied)

        for run_id in expected:
            run_dir = run_root / run_id
            for name in RUN_EVIDENCE_FILES:
                path = run_dir / name
                if path.is_file():
                    copy_file(path, tmp_root, Path("runs") / run_id / name, copied)

        bundle_manifest = {
            "stage": stage,
            "source_root": str(root),
            "expected_cells": len(expected),
            "acceptance_report": str(report_path),
            "summary_csv": str(csv_path),
            "wrapper_status": str(status_path) if status_path else "",
            "frozen_inputs": str(frozen_inputs_path) if frozen_inputs_path.is_file() else "",
            "stage_plan": str(stage_plan_path) if str(stage_plan_path) else "",
            "stage_plan_manifest_sha256": (
                ((stage_plan.get("manifest") or {}).get("sha256", ""))
                if isinstance(stage_plan.get("manifest"), dict) else ""
            ),
            "runtime_noise_summary": str(noise_summary_path) if noise_summary_path else "",
            "runtime_noise_warnings": noise_summary.get("warnings", []) if noise_summary else [],
            "warnings": warnings,
            "runs": run_manifest,
            "copied_files": copied,
        }
        (tmp_root / "bundle_manifest.json").write_text(
            json.dumps(bundle_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

        with tarfile.open(out_path, "w:gz") as tar:
            tar.add(tmp_root, arcname=tmp_root.name)

    print(f"bundle written: {out_path}")
    if warnings:
        print("warnings:")
        for warning in warnings:
            print(f"- {warning}")
    print(f"files copied: {len(copied)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
