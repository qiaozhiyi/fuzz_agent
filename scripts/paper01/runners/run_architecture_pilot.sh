#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MANIFEST="${MANIFEST:-${REPO_ROOT}/experiments/manifests/paper_architecture_pilot.yaml}"
FUZZPILOT_BIN="${FUZZPILOT_BIN:-${REPO_ROOT}/build/fuzzpilot}"
STAGE="${1:-A}"
DRY_RUN="${DRY_RUN:-0}"
RERUN_COMPLETED="${RERUN_COMPLETED:-0}"

cd "${REPO_ROOT}"

if [[ ! -f "${MANIFEST}" ]]; then
  echo "missing manifest: ${MANIFEST}" >&2
  exit 2
fi

if [[ "${DRY_RUN}" != "1" && ! -x "${FUZZPILOT_BIN}" ]]; then
  echo "missing fuzzpilot binary: ${FUZZPILOT_BIN}" >&2
  exit 2
fi

python3 - "${MANIFEST}" "${STAGE}" "${FUZZPILOT_BIN}" "${DRY_RUN}" "${RERUN_COMPLETED}" <<'PY'
import json
import hashlib
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import time
from urllib.parse import urlparse

try:
    import yaml
except ImportError as exc:
    raise SystemExit(f"PyYAML is required to read the manifest: {exc}")

manifest_path, stage_name, fuzzpilot_bin, dry_run, rerun_completed = sys.argv[1:6]
manifest = yaml.safe_load(open(manifest_path, encoding="utf-8")) or {}
stage = (manifest.get("stages") or {}).get(stage_name)
if not stage:
    raise SystemExit(f"unknown stage {stage_name!r}")

repo_root = pathlib.Path.cwd()
out_root = repo_root / manifest["defaults"]["out_root"]
out_root.mkdir(parents=True, exist_ok=True)

llm_modes = {
    "full-agent",
    "ai-direct",
    "no-static-analysis",
    "no-semantic-context",
    "single-agent-coordinator",
    "single-agent-dictionary",
    "no-mutator",
}

cpu_count = os.cpu_count() or 1
slots = list(range(cpu_count))
if len(slots) >= 4:
    # Leave CPU 0 for the OS/controller when this host has four cores.
    fuzz_slots = slots[1:]
else:
    fuzz_slots = slots

def command_with_affinity(cpu, cmd):
    if shutil.which("taskset"):
        return ["taskset", "-c", str(cpu)] + cmd
    return cmd

def run_text(cmd, timeout=5):
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"unavailable: {exc}"
    if not result.stdout.strip():
        return ""
    first = result.stdout.strip().splitlines()[0]
    return re.sub(r"\x1b\[[0-9;]*m", "", first)[:240]

def afl_version_text(afl):
    text = run_text([afl, "-h"], timeout=3)
    try:
        result = subprocess.run(
            [afl, "-h"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=3,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return text or "unavailable"
    for line in result.stdout.splitlines():
        clean = re.sub(r"\x1b\[[0-9;]*m", "", line).strip()
        if clean.startswith("afl-fuzz"):
            return clean[:240]
    return text or "unavailable"

def file_sha256(path):
    path = pathlib.Path(path)
    if not path.exists() or not path.is_file():
        return ""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def git_diff_sha256():
    try:
        result = subprocess.run(
            ["git", "diff", "--binary", "--no-ext-diff"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=20,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return hashlib.sha256(result.stdout).hexdigest()

def cpu_model_name():
    cpuinfo = pathlib.Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(errors="replace").splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    return platform.processor() or "unknown"

def mem_total_kib():
    meminfo = pathlib.Path("/proc/meminfo")
    if meminfo.exists():
        for line in meminfo.read_text(errors="replace").splitlines():
            if line.startswith("MemTotal:"):
                parts = line.split()
                return int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0
    return 0

def git_summary():
    dirty = bool(run_text(["git", "status", "--porcelain"]))
    return {
        "head": run_text(["git", "rev-parse", "HEAD"]),
        "short_head": run_text(["git", "rev-parse", "--short", "HEAD"]),
        "dirty": dirty,
        "tracked_diff_sha256": git_diff_sha256() if dirty else "",
    }

def host_summary():
    return {
        "hostname": platform.node(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "cpu_count": cpu_count,
        "cpu_model": cpu_model_name(),
        "mem_total_kib": mem_total_kib(),
        "reserved_cpu": 0 if len(slots) >= 4 else None,
        "fuzz_slots": fuzz_slots,
        "taskset_available": shutil.which("taskset") is not None,
    }

def tool_summary():
    afl = shutil.which("afl-fuzz") or ""
    return {
        "fuzzpilot_bin": str(pathlib.Path(fuzzpilot_bin).resolve()),
        "fuzzpilot_sha256": file_sha256(fuzzpilot_bin),
        "afl_fuzz": afl,
        "afl_fuzz_version": afl_version_text(afl) if afl else "unavailable",
        "python": sys.version.split()[0],
    }

def target_config_summary(config_path):
    path = pathlib.Path(config_path)
    if not path.exists():
        return {"config_path": config_path}
    cfg = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    target_cfg = cfg.get("target") or {}
    model_cfg = cfg.get("model_api") or {}
    static_cfg = cfg.get("static_analysis") or {}
    endpoint = model_cfg.get("endpoint", "")
    binary = target_cfg.get("binary", "")
    context_path = static_cfg.get("context_path", "")
    return {
        "config_path": config_path,
        "target_binary": binary,
        "target_binary_sha256": file_sha256(binary),
        "target_input_dir": target_cfg.get("input_dir", ""),
        "target_dict": target_cfg.get("dict", ""),
        "static_analysis_enabled": bool(static_cfg.get("enabled", False)),
        "static_backend": static_cfg.get("backend", ""),
        "static_context_path": context_path,
        "static_context_sha256": file_sha256(context_path) if context_path else "",
        "model_provider": model_cfg.get("provider", ""),
        "model_name": model_cfg.get("model", ""),
        "model_endpoint_host": urlparse(endpoint).netloc if endpoint else "",
        "api_key_env": model_cfg.get("api_key_env", ""),
        "api_key_env_state": "set" if os.environ.get(model_cfg.get("api_key_env", "")) else "unset",
    }

def blocked_mode_order(modes, repeat, target_id):
    modes = list(modes)
    seed = repeat + sum(ord(c) for c in target_id)
    shift = seed % len(modes)
    return modes[shift:] + modes[:shift]

def copy_inner_artifacts(run_dir):
    work_dir = run_dir / "work"
    inner = next(work_dir.glob("run_*"), None) if work_dir.exists() else None
    if not inner:
        return
    for name in [
        "coverage.csv",
        "events.jsonl",
        "agent_decisions.jsonl",
        "agent_memory.jsonl",
        "fuzzpilot.sqlite",
        "main_launch.sh",
        "report.md",
        "recipe_rewards.jsonl",
    ]:
        src = inner / name
        if src.exists():
            shutil.copy2(src, run_dir / name)
    stats_candidates = [
        inner / "main_out/default/fuzzer_stats",
        inner / "main_out_restart_0/default/fuzzer_stats",
        inner / "main_out_restart_1/default/fuzzer_stats",
    ]
    for stats in reversed(stats_candidates):
        if stats.exists():
            shutil.copy2(stats, run_dir / "fuzzer_stats")
            break

def secret_safe_env_summary():
    return {
        "FUZZPILOT_MODEL_API_KEY": "set" if os.environ.get("FUZZPILOT_MODEL_API_KEY") else "unset",
        "FUZZPILOT_MODEL_ENDPOINT_HOST": urlparse(
            os.environ.get("FUZZPILOT_MODEL_ENDPOINT", "")
        ).netloc if os.environ.get("FUZZPILOT_MODEL_ENDPOINT") else "",
    }

modes = stage.get("modes") or manifest["modes"]
targets = manifest["targets"]
repeats = int(stage["repeats"])
budget_sec = int(stage["budget_sec"])
llm_parallel = max(1, int((manifest.get("defaults") or {}).get("llm_parallel", 1)))
non_llm_parallel = max(1, int((manifest.get("defaults") or {}).get("non_llm_parallel", 1)))
non_llm_parallel = min(non_llm_parallel, len(fuzz_slots))
llm_parallel = min(llm_parallel, len(fuzz_slots))

print(f"architecture pilot stage={stage_name} repeats={repeats} budget_sec={budget_sec}")
print(f"host cpu_count={cpu_count} fuzz_slots={fuzz_slots}")
print(f"parallelism llm={llm_parallel} non_llm={non_llm_parallel}")
print(f"resume policy rerun_completed={rerun_completed}")

run_environment = {
    "git": git_summary(),
    "host": host_summary(),
    "tools": tool_summary(),
}

active_non_llm = []
available_non_llm_cpus = list(fuzz_slots[:non_llm_parallel])

def run_id_for(repeat, target_id, mode):
    return f"arch_{stage_name.lower()}_{target_id}_{mode}_r{repeat:02d}"

def text_if_exists(path):
    try:
        return pathlib.Path(path).read_text(encoding="utf-8").strip()
    except OSError:
        return ""

def completed_success(run_dir):
    return (
        text_if_exists(run_dir / "status") == "completed"
        and text_if_exists(run_dir / "exit_code") == "0"
    )

def patch_resume_metadata(run_dir, run_order_index, blocked_order_index):
    meta_path = run_dir / "runner_metadata.json"
    if not meta_path.exists():
        return
    try:
        metadata = json.loads(meta_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return
    metadata["run_order_index"] = run_order_index
    metadata["blocked_order_index"] = blocked_order_index
    metadata.setdefault("runner_policy", {})
    metadata["runner_policy"]["rerun_completed"] = rerun_completed == "1"
    metadata["runner_policy"]["completed_success_skip_default"] = True
    meta_path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

def build_cell(repeat, target, mode, cpu, run_order_index, blocked_order_index):
    run_id = f"arch_{stage_name.lower()}_{target['id']}_{mode}_r{repeat:02d}"
    run_dir = out_root / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    status_path = run_dir / "status"
    base_cmd = [
        fuzzpilot_bin,
        "run",
        "",
        "--config",
        target["config"],
        "--ablation",
        mode,
        "--main-budget-sec",
        str(budget_sec),
        "--work-dir",
        str(run_dir / "work"),
    ]
    cmd = command_with_affinity(cpu, base_cmd)
    metadata = {
        "run_id": run_id,
        "paper": manifest.get("paper"),
        "stage": stage_name,
        "target": target["id"],
        "target_name": target["name"],
        "target_config": target["config"],
        "mode": mode,
        "repeat": repeat,
        "run_order_index": run_order_index,
        "blocked_order_index": blocked_order_index,
        "cpu": cpu,
        "cpu_count": cpu_count,
        "fuzz_slots": fuzz_slots,
        "budget_sec": budget_sec,
        "cmd": cmd,
        "env": secret_safe_env_summary(),
        "git": run_environment["git"],
        "host": run_environment["host"],
        "tools": run_environment["tools"],
        "target_artifacts": target_config_summary(target["config"]),
        "runner_policy": {
            "rerun_completed": rerun_completed == "1",
            "completed_success_skip_default": True,
        },
        "started_at_unix": int(time.time()),
    }
    (run_dir / "runner_metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return {
        "run_id": run_id,
        "run_dir": run_dir,
        "status_path": status_path,
        "mode": mode,
        "target": target["id"],
        "cpu": cpu,
        "cmd": cmd,
    }

def finish_process(cell, proc):
    rc = proc.wait()
    (cell["run_dir"] / "exit_code").write_text(f"{rc}\n", encoding="utf-8")
    copy_inner_artifacts(cell["run_dir"])
    cell["status_path"].write_text("completed\n" if rc == 0 else "failed\n", encoding="utf-8")
    print(f"DONE {cell['run_id']} rc={rc}")
    return rc

def wait_for_one_non_llm():
    while active_non_llm:
        for index, (cell, proc, stdout_file, stderr_file) in enumerate(active_non_llm):
            rc = proc.poll()
            if rc is None:
                continue
            stdout_file.close()
            stderr_file.close()
            (cell["run_dir"] / "exit_code").write_text(f"{rc}\n", encoding="utf-8")
            copy_inner_artifacts(cell["run_dir"])
            cell["status_path"].write_text("completed\n" if rc == 0 else "failed\n", encoding="utf-8")
            print(f"DONE {cell['run_id']} rc={rc}")
            available_non_llm_cpus.append(cell["cpu"])
            available_non_llm_cpus.sort()
            del active_non_llm[index]
            return rc
        time.sleep(5)
    return 0

def drain_non_llm():
    while active_non_llm:
        wait_for_one_non_llm()

def launch_non_llm(cell):
    cpu = available_non_llm_cpus.pop(0)
    if cpu != cell["cpu"]:
        raise RuntimeError("internal runner CPU allocation mismatch")
    out = open(cell["run_dir"] / "stdout.log", "w", encoding="utf-8")
    err = open(cell["run_dir"] / "stderr.log", "w", encoding="utf-8")
    cell["status_path"].write_text("running\n", encoding="utf-8")
    print(f"RUN {cell['run_id']} cpu={cell['cpu']} mode={cell['mode']} target={cell['target']}")
    proc = subprocess.Popen(cell["cmd"], stdout=out, stderr=err)
    active_non_llm.append((cell, proc, out, err))

run_order_index = 0
for repeat in range(1, repeats + 1):
    for target in targets:
        mode_order = blocked_mode_order(modes, repeat, target["id"])
        for blocked_order_index, mode in enumerate(mode_order, start=1):
            run_order_index += 1
            needs_key = mode in llm_modes
            run_id = run_id_for(repeat, target["id"], mode)
            run_dir = out_root / run_id
            if rerun_completed != "1" and completed_success(run_dir):
                patch_resume_metadata(run_dir, run_order_index, blocked_order_index)
                print(f"SKIP-COMPLETED {run_id}")
                continue

            if dry_run == "1":
                cpu = fuzz_slots[(repeat + len(target["id"]) + len(mode)) % len(fuzz_slots)]
                cell = build_cell(repeat, target, mode, cpu, run_order_index, blocked_order_index)
                cell["status_path"].write_text("dry-run\n", encoding="utf-8")
                print("DRY:", " ".join(cell["cmd"]))
                continue

            if needs_key and not os.environ.get("FUZZPILOT_MODEL_API_KEY"):
                cpu = fuzz_slots[-1]
                cell = build_cell(repeat, target, mode, cpu, run_order_index, blocked_order_index)
                cell["status_path"].write_text("skipped-missing-api-key\n", encoding="utf-8")
                skip_meta_path = cell["run_dir"] / "runner_metadata.json"
                skip_meta = json.loads(skip_meta_path.read_text(encoding="utf-8"))
                skip_meta["skip_reason"] = "missing_api_key"
                skip_meta_path.write_text(
                    json.dumps(skip_meta, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                print(f"SKIP {cell['run_id']}: missing FUZZPILOT_MODEL_API_KEY")
                continue

            if needs_key:
                drain_non_llm()
                cpu = fuzz_slots[-1]
                cell = build_cell(repeat, target, mode, cpu, run_order_index, blocked_order_index)
                print(f"RUN {cell['run_id']} cpu={cpu} mode={mode} target={target['id']}")
                cell["status_path"].write_text("running\n", encoding="utf-8")
                with open(cell["run_dir"] / "stdout.log", "w", encoding="utf-8") as out, \
                     open(cell["run_dir"] / "stderr.log", "w", encoding="utf-8") as err:
                    proc = subprocess.Popen(cell["cmd"], stdout=out, stderr=err)
                    finish_process(cell, proc)
                # Reduce API-rate and thermal cross-talk between LLM-bearing cells.
                time.sleep(20)
            else:
                while len(active_non_llm) >= non_llm_parallel or not available_non_llm_cpus:
                    wait_for_one_non_llm()
                cpu = available_non_llm_cpus[0] if available_non_llm_cpus else fuzz_slots[0]
                cell = build_cell(repeat, target, mode, cpu, run_order_index, blocked_order_index)
                launch_non_llm(cell)

drain_non_llm()
PY
