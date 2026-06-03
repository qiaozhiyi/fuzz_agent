#!/usr/bin/env python3
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


REQUIRED_DOCS = [
    "docs/papers/architecture_pilot_runbook.md",
    "docs/papers/architecture_pilot_claim_matrix.md",
    "docs/papers/architecture_pilot_paper_skeleton.md",
    "docs/papers/architecture_pilot_related_work.md",
]

PYTHON_SCRIPTS = [
    "scripts/paper01/architecture_pilot_readiness.py",
    "scripts/paper01/architecture_noise_monitor.py",
    "scripts/paper01/model_auth_smoke.py",
    "scripts/paper01/architecture_freeze_inputs.py",
    "scripts/paper01/architecture_pilot_summary.py",
    "scripts/paper01/build_static_context_from_dict.py",
    "scripts/paper01/architecture_stage_status.py",
    "scripts/paper01/architecture_pilot_bundle.py",
]

SHELL_SCRIPTS = [
    "scripts/paper01/with_model_key.sh",
    "scripts/paper01/preflight_architecture_pilot.sh",
    "scripts/paper01/run_architecture_stage.sh",
    "scripts/paper01/runners/run_architecture_pilot.sh",
]

EXECUTABLE_SCRIPTS = [
    "scripts/paper01/with_model_key.sh",
    "scripts/paper01/architecture_pilot_readiness.py",
    "scripts/paper01/architecture_noise_monitor.py",
    "scripts/paper01/model_auth_smoke.py",
    "scripts/paper01/architecture_freeze_inputs.py",
    "scripts/paper01/preflight_architecture_pilot.sh",
    "scripts/paper01/run_architecture_stage.sh",
    "scripts/paper01/runners/run_architecture_pilot.sh",
    "scripts/paper01/architecture_stage_status.py",
    "scripts/paper01/architecture_pilot_bundle.py",
]

SECRET_SCAN_ROOTS = [
    "include",
    "src",
    "tools",
    "scripts",
    "tests",
    "experiments",
    "docs",
]

FORBIDDEN_SECRET_PATTERNS = [
    re.compile(r"[0-9a-fA-F]{32}\.[A-Za-z0-9_-]{12,}"),
    re.compile(r"Bearer[ \t]+[A-Za-z0-9._-]{12,}"),
]


def run_cmd(cmd: list[str], env: dict[str, str] | None = None, timeout: int | None = None) -> dict[str, object]:
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            env=env,
            timeout=timeout,
        )
        return {
            "cmd": cmd,
            "returncode": result.returncode,
            "output": result.stdout.strip(),
        }
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {
            "cmd": cmd,
            "returncode": 124 if isinstance(exc, subprocess.TimeoutExpired) else 127,
            "output": str(exc),
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


def stage_estimate(manifest: dict, stage_name: str) -> dict[str, float | int]:
    llm_modes = {
        "full-agent",
        "ai-direct",
        "no-static-analysis",
        "no-semantic-context",
        "single-agent-coordinator",
        "single-agent-dictionary",
        "no-mutator",
    }
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    defaults = manifest.get("defaults") or {}
    repeats = int(stage.get("repeats") or 0)
    budget_sec = int(stage.get("budget_sec") or 0)
    targets = manifest.get("targets") or []
    modes = stage_modes(manifest, stage_name)
    llm_parallel = max(1, int(defaults.get("llm_parallel") or 1))
    non_llm_parallel = max(1, int(defaults.get("non_llm_parallel") or 1))
    llm_cells = sum(1 for mode in modes if mode in llm_modes) * len(targets) * repeats
    total_cells = len(targets) * len(modes) * repeats
    non_llm_cells = total_cells - llm_cells
    return {
        "targets": len(targets),
        "modes": len(modes),
        "repeats": repeats,
        "budget_sec": budget_sec,
        "total_cells": total_cells,
        "llm_cells": llm_cells,
        "non_llm_cells": non_llm_cells,
        "llm_parallel": llm_parallel,
        "non_llm_parallel": non_llm_parallel,
        "core_hours": total_cells * budget_sec / 3600.0,
        "conservative_wall_hours": (
            (llm_cells * budget_sec / llm_parallel)
            + (non_llm_cells * budget_sec / non_llm_parallel)
        ) / 3600.0,
    }


def latest_status(root: Path, stage_name: str) -> dict:
    paths = sorted((root / "logs").glob(f"stage_{stage_name.lower()}_*_status.json"))
    if not paths:
        return {}
    newest = max(paths, key=lambda path: path.stat().st_mtime)
    try:
        data = json.loads(newest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        data = {}
    data["_path"] = str(newest)
    return data


def status_is_clean(status: dict) -> bool:
    return (
        int(status.get("preflight_exit_code", 1)) == 0
        and int(status.get("runner_exit_code", 1)) == 0
        and int(status.get("summary_exit_code", 1)) == 0
    )


def scan_forbidden_secrets(root: Path) -> list[str]:
    hits: list[str] = []
    for scan_root in SECRET_SCAN_ROOTS:
        path = root / scan_root
        if not path.exists():
            continue
        for file_path in path.rglob("*"):
            if not file_path.is_file() or "__pycache__" in file_path.parts:
                continue
            try:
                text = file_path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for line_no, line in enumerate(text.splitlines(), start=1):
                if any(pattern.search(line) for pattern in FORBIDDEN_SECRET_PATTERNS):
                    hits.append(f"{file_path}:{line_no}")
                    if len(hits) >= 20:
                        return hits
    return hits


def add_check(checks: list[dict[str, object]], ok: bool, name: str, detail: str, level: str = "fail") -> None:
    checks.append({
        "ok": ok,
        "name": name,
        "detail": detail,
        "level": "pass" if ok else level,
    })


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a paper-readiness audit before a FuzzPilot architecture stage."
    )
    parser.add_argument("stage", nargs="?", default="S", help="Stage name: S, A, B, or C.")
    parser.add_argument(
        "--manifest",
        default="experiments/manifests/paper_architecture_pilot.yaml",
        help="Architecture pilot manifest.",
    )
    parser.add_argument(
        "--root",
        default="results/fuzzpilot_architecture_pilot",
        help="Architecture pilot result root.",
    )
    parser.add_argument("--skip-build", action="store_true", help="Do not run cmake --build.")
    parser.add_argument("--skip-tests", action="store_true", help="Do not run CTest.")
    parser.add_argument(
        "--allow-existing-results",
        action="store_true",
        help="Allow an existing architecture pilot result directory.",
    )
    parser.add_argument(
        "--real-run",
        action="store_true",
        help="Require API key and previous-stage gates as if launching a real paper stage.",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stage = args.stage.upper()
    root = Path.cwd()
    result_root = Path(args.root)
    manifest_path = Path(args.manifest)
    checks: list[dict[str, object]] = []
    commands: list[dict[str, object]] = []

    add_check(checks, manifest_path.is_file(), "manifest exists", str(manifest_path))
    manifest = load_manifest(manifest_path) if manifest_path.is_file() else {}
    stage_cfg = (manifest.get("stages") or {}).get(stage) or {}
    add_check(checks, bool(stage_cfg), "stage exists", stage)
    estimate = stage_estimate(manifest, stage) if stage_cfg else {}

    for doc in REQUIRED_DOCS:
        path = Path(doc)
        add_check(checks, path.is_file() and path.stat().st_size > 0, "required paper doc", doc)

    for script in EXECUTABLE_SCRIPTS:
        path = Path(script)
        add_check(checks, path.is_file() and os.access(path, os.X_OK), "executable script", script)

    cpu_count = os.cpu_count() or 1
    fuzz_slots = max(1, cpu_count - 1) if cpu_count >= 4 else cpu_count
    add_check(
        checks,
        cpu_count >= 4,
        "host CPU class",
        f"cpu_count={cpu_count}; fuzz_slots={fuzz_slots}",
        level="warn",
    )
    if estimate:
        add_check(
            checks,
            int(estimate["non_llm_parallel"]) <= fuzz_slots,
            "non-LLM parallelism fits fixed fuzz slots",
            f"non_llm_parallel={estimate['non_llm_parallel']} fuzz_slots={fuzz_slots}",
        )
        add_check(
            checks,
            int(estimate["llm_parallel"]) == 1,
            "LLM-bearing cells are serialized",
            f"llm_parallel={estimate['llm_parallel']}",
        )

    if args.real_run:
        add_check(
            checks,
            bool(os.environ.get("FUZZPILOT_MODEL_API_KEY")),
            "model API key set for real run",
            "FUZZPILOT_MODEL_API_KEY=set" if os.environ.get("FUZZPILOT_MODEL_API_KEY") else "FUZZPILOT_MODEL_API_KEY=unset",
        )
    else:
        add_check(
            checks,
            bool(os.environ.get("FUZZPILOT_MODEL_API_KEY")),
            "model API key currently set",
            "FUZZPILOT_MODEL_API_KEY=set" if os.environ.get("FUZZPILOT_MODEL_API_KEY") else "FUZZPILOT_MODEL_API_KEY=unset",
            level="warn",
        )

    if stage in {"B", "C"} and args.real_run:
        frozen_inputs = run_cmd(
            ["scripts/paper01/architecture_freeze_inputs.py", stage, "--check"],
            timeout=60,
        )
        commands.append(frozen_inputs)
        add_check(
            checks,
            int(frozen_inputs["returncode"]) == 0,
            "frozen Stage inputs match",
            f"exit={frozen_inputs['returncode']}",
        )
        previous = "A" if stage == "B" else "B"
        previous_status = latest_status(result_root, previous)
        previous_report = result_root / f"stage_{previous.lower()}_acceptance.md"
        report_clean = previous_report.is_file() and "- [FAIL]" not in previous_report.read_text(encoding="utf-8", errors="ignore")
        add_check(
            checks,
            status_is_clean(previous_status),
            f"previous Stage {previous} wrapper status clean",
            str(previous_status.get("_path", "")) or "missing",
        )
        add_check(
            checks,
            report_clean,
            f"previous Stage {previous} acceptance report clean",
            str(previous_report),
        )

    if args.real_run:
        auth_smoke = run_cmd(
            [
                "scripts/paper01/model_auth_smoke.py",
                "--config",
                "experiments/targets/libxml2/config_glm.yaml",
            ],
            timeout=60,
        )
        commands.append(auth_smoke)
        add_check(
            checks,
            int(auth_smoke["returncode"]) == 0,
            "model auth smoke",
            f"exit={auth_smoke['returncode']}",
        )

    preflight_env = os.environ.copy()
    preflight_env["ALLOW_EXISTING_RESULTS"] = "1" if args.allow_existing_results else "0"
    preflight = run_cmd(
        ["scripts/paper01/preflight_architecture_pilot.sh", stage],
        env=preflight_env,
        timeout=60,
    )
    commands.append(preflight)
    add_check(
        checks,
        int(preflight["returncode"]) == 0,
        "architecture preflight",
        f"exit={preflight['returncode']}",
    )

    bash_check = run_cmd(["bash", "-n", *SHELL_SCRIPTS], timeout=30)
    commands.append(bash_check)
    add_check(checks, int(bash_check["returncode"]) == 0, "shell syntax", f"exit={bash_check['returncode']}")

    py_check = run_cmd(["python3", "-m", "py_compile", *PYTHON_SCRIPTS], timeout=60)
    commands.append(py_check)
    add_check(checks, int(py_check["returncode"]) == 0, "python syntax", f"exit={py_check['returncode']}")

    diff_check = run_cmd(["git", "diff", "--check"], timeout=60)
    commands.append(diff_check)
    add_check(checks, int(diff_check["returncode"]) == 0, "git diff whitespace", f"exit={diff_check['returncode']}")

    if not args.skip_build:
        build = run_cmd(["cmake", "--build", "build", "--parallel", "2"], timeout=300)
        commands.append(build)
        add_check(checks, int(build["returncode"]) == 0, "cmake build", f"exit={build['returncode']}")
    if not args.skip_tests:
        tests = run_cmd(["ctest", "--test-dir", "build", "--output-on-failure"], timeout=300)
        commands.append(tests)
        add_check(checks, int(tests["returncode"]) == 0, "ctest", f"exit={tests['returncode']}")

    secret_hits = scan_forbidden_secrets(root)
    add_check(
        checks,
        not secret_hits,
        "forbidden secret scan",
        "no hits" if not secret_hits else ", ".join(secret_hits[:10]),
    )

    hard_failures = [check for check in checks if not check["ok"] and check["level"] == "fail"]
    warnings = [check for check in checks if not check["ok"] and check["level"] == "warn"]
    payload = {
        "stage": stage,
        "ready": not hard_failures,
        "hard_failures": hard_failures,
        "warnings": warnings,
        "estimate": estimate,
        "checks": checks,
        "commands": [
            {
                "cmd": item["cmd"],
                "returncode": item["returncode"],
                "output_tail": "\n".join(str(item.get("output", "")).splitlines()[-20:]),
            }
            for item in commands
        ],
    }

    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(f"FuzzPilot architecture readiness: stage {stage}")
        if estimate:
            print(
                "estimate: {cells} cells, {core:.1f} core-hours, conservative wall-clock {wall:.1f} h".format(
                    cells=estimate["total_cells"],
                    core=estimate["core_hours"],
                    wall=estimate["conservative_wall_hours"],
                )
            )
        for check in checks:
            prefix = "PASS" if check["ok"] else ("WARN" if check["level"] == "warn" else "FAIL")
            print(f"[{prefix}] {check['name']}: {check['detail']}")
        print("ready=yes" if payload["ready"] else "ready=no")

    return 0 if payload["ready"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
