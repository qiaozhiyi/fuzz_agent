#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE="${1:-A}"
RESULT_ROOT="${RESULT_ROOT:-${REPO_ROOT}/results/fuzzpilot_architecture_pilot}"
RUN_ROOT="${RUN_ROOT:-${RESULT_ROOT}/runs}"
LOG_ROOT="${LOG_ROOT:-${RESULT_ROOT}/logs}"
RESUME="${RESUME:-1}"
DRY_RUN="${DRY_RUN:-0}"
REQUIRE_API_KEY="${REQUIRE_API_KEY:-1}"
if [[ -z "${REQUIRE_PREVIOUS_STAGE_PASS+x}" ]]; then
  if [[ "${DRY_RUN}" == "1" ]]; then
    REQUIRE_PREVIOUS_STAGE_PASS="0"
  else
    REQUIRE_PREVIOUS_STAGE_PASS="1"
  fi
fi
if [[ -z "${FAIL_ON_GATE_FAIL+x}" ]]; then
  if [[ "${DRY_RUN}" == "1" ]]; then
    FAIL_ON_GATE_FAIL="0"
  else
    FAIL_ON_GATE_FAIL="1"
  fi
fi
if [[ -z "${REQUIRE_FROZEN_INPUTS+x}" ]]; then
  if [[ "${DRY_RUN}" == "1" || "${STAGE^^}" == "S" || "${STAGE^^}" == "A" ]]; then
    REQUIRE_FROZEN_INPUTS="0"
  else
    REQUIRE_FROZEN_INPUTS="1"
  fi
fi

cd "${REPO_ROOT}"

mkdir -p "${LOG_ROOT}"
started_at="$(date -u +%Y%m%dT%H%M%SZ)"
log_prefix="${LOG_ROOT}/stage_${STAGE,,}_${started_at}"
preflight_log="${log_prefix}_preflight.log"
runner_log="${log_prefix}_runner.log"
summary_log="${log_prefix}_summary.log"
noise_log="${log_prefix}_noise.jsonl"
noise_summary="${log_prefix}_noise_summary.json"
stage_plan="${log_prefix}_stage_plan.json"
status_json="${log_prefix}_status.json"
csv_path="${RESULT_ROOT}/stage_${STAGE,,}_summary.csv"
report_path="${RESULT_ROOT}/stage_${STAGE,,}_acceptance.md"
noise_monitor_pid=""

stop_noise_monitor() {
  if [[ -n "${noise_monitor_pid}" ]] && kill -0 "${noise_monitor_pid}" 2>/dev/null; then
    kill -TERM "${noise_monitor_pid}" 2>/dev/null || true
    wait "${noise_monitor_pid}" 2>/dev/null || true
  fi
}
trap stop_noise_monitor EXIT

if [[ "${RESUME}" == "1" ]]; then
  export ALLOW_EXISTING_RESULTS="${ALLOW_EXISTING_RESULTS:-1}"
else
  export ALLOW_EXISTING_RESULTS="${ALLOW_EXISTING_RESULTS:-0}"
fi

printf 'stage=%s dry_run=%s resume=%s\n' "${STAGE}" "${DRY_RUN}" "${RESUME}" | tee "${status_json}.tmp"
printf 'logs=%s\n' "${LOG_ROOT}" | tee -a "${status_json}.tmp"
printf 'runs=%s\n' "${RUN_ROOT}" | tee -a "${status_json}.tmp"

python3 - "${stage_plan}" "${STAGE}" "${DRY_RUN}" "${RESUME}" \
  "${REQUIRE_API_KEY}" "${REQUIRE_PREVIOUS_STAGE_PASS}" "${FAIL_ON_GATE_FAIL}" \
  "${NOISE_MONITOR_INTERVAL_SEC:-30}" "${NOISE_MAX_LOAD1_PER_CPU:-1.25}" \
  "${NOISE_MIN_MEM_AVAILABLE_KIB:-1048576}" "${NOISE_MIN_DISK_FREE_GIB:-20}" <<'PY'
import hashlib
import json
import os
import pathlib
import subprocess
import sys

try:
    import yaml
except ImportError as exc:
    raise SystemExit(f"PyYAML is required to write stage plan: {exc}")

(
    plan_path,
    stage_name,
    dry_run,
    resume,
    require_api_key,
    require_previous_stage_pass,
    fail_on_gate_fail,
    noise_interval_sec,
    noise_max_load1_per_cpu,
    noise_min_mem_available_kib,
    noise_min_disk_free_gib,
) = sys.argv[1:12]

repo_root = pathlib.Path.cwd()
manifest_path = repo_root / "experiments/manifests/paper_architecture_pilot.yaml"
manifest_bytes = manifest_path.read_bytes()
manifest = yaml.safe_load(manifest_bytes.decode("utf-8")) or {}
stage = (manifest.get("stages") or {}).get(stage_name) or {}
targets = manifest.get("targets") or []
modes = list(stage.get("modes") or manifest.get("modes") or [])
repeats = int(stage.get("repeats") or 0)
budget_sec = int(stage.get("budget_sec") or 0)
defaults = manifest.get("defaults") or {}
llm_modes = {
    "full-agent",
    "ai-direct",
    "no-static-analysis",
    "no-semantic-context",
    "single-agent-coordinator",
    "single-agent-dictionary",
    "no-mutator",
}
llm_parallel = max(1, int(defaults.get("llm_parallel") or 1))
non_llm_parallel = max(1, int(defaults.get("non_llm_parallel") or 1))
llm_cells = sum(1 for mode in modes if mode in llm_modes) * len(targets) * repeats
total_cells = len(targets) * len(modes) * repeats
non_llm_cells = total_cells - llm_cells
cpu_count = os.cpu_count() or 1
fuzz_slots = list(range(1, cpu_count)) if cpu_count >= 4 else list(range(cpu_count))

def run_text(cmd: list[str]) -> str:
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return result.stdout.strip()

def expected_run_ids() -> list[str]:
    run_ids = []
    for repeat in range(1, repeats + 1):
        for target in targets:
            target_id = target.get("id", "")
            for mode in modes:
                run_ids.append(f"arch_{stage_name.lower()}_{target_id}_{mode}_r{repeat:02d}")
    return run_ids

git_dirty = bool(run_text(["git", "status", "--porcelain"]))
plan = {
    "stage": stage_name,
    "created_at_utc": run_text(["date", "-u", "+%Y-%m-%dT%H:%M:%SZ"]),
    "dry_run": dry_run == "1",
    "resume": resume == "1",
    "policy": {
        "require_api_key": require_api_key == "1",
        "require_previous_stage_pass": require_previous_stage_pass == "1",
        "fail_on_gate_fail": fail_on_gate_fail == "1",
    },
    "manifest": {
        "path": str(manifest_path),
        "sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "paper": manifest.get("paper", ""),
        "version": manifest.get("version", ""),
    },
    "stage_config": {
        "description": stage.get("description", ""),
        "acceptance_profile": stage.get("acceptance_profile", "paper"),
        "repeats": repeats,
        "budget_sec": budget_sec,
        "targets": [target.get("id", "") for target in targets],
        "modes": modes,
        "expected_run_ids": expected_run_ids(),
    },
    "resource_plan": {
        "cpu_count": cpu_count,
        "reserved_cpu": 0 if cpu_count >= 4 else None,
        "fuzz_slots": fuzz_slots,
        "llm_parallel": llm_parallel,
        "non_llm_parallel": non_llm_parallel,
        "llm_cells": llm_cells,
        "non_llm_cells": non_llm_cells,
        "total_cells": total_cells,
        "core_hours": total_cells * budget_sec / 3600.0,
        "conservative_wall_hours": (
            (llm_cells * budget_sec / llm_parallel)
            + (non_llm_cells * budget_sec / non_llm_parallel)
        ) / 3600.0,
    },
    "runtime_noise_policy": {
        "interval_sec": float(noise_interval_sec),
        "max_load1_per_cpu": float(noise_max_load1_per_cpu),
        "min_mem_available_kib": int(noise_min_mem_available_kib),
        "min_disk_free_gib": float(noise_min_disk_free_gib),
    },
    "model_secret_state": {
        "FUZZPILOT_MODEL_API_KEY": "set" if os.environ.get("FUZZPILOT_MODEL_API_KEY") else "unset",
    },
    "git": {
        "head": run_text(["git", "rev-parse", "HEAD"]),
        "short_head": run_text(["git", "rev-parse", "--short", "HEAD"]),
        "dirty": git_dirty,
    },
}
pathlib.Path(plan_path).write_text(
    json.dumps(plan, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

echo "[stage] preflight ${STAGE}"
set +e
scripts/paper01/preflight_architecture_pilot.sh "${STAGE}" 2>&1 | tee "${preflight_log}"
preflight_rc=${PIPESTATUS[0]}
set -e
if [[ "${preflight_rc}" -eq 0 && "${REQUIRE_FROZEN_INPUTS}" == "1" ]]; then
  if ! scripts/paper01/architecture_freeze_inputs.py "${STAGE}" --check 2>&1 | tee -a "${preflight_log}"; then
    echo "[FAIL] frozen Stage inputs are required for real Stage B/C runs" | tee -a "${preflight_log}"
    echo "[FAIL] create them before launch: scripts/paper01/architecture_freeze_inputs.py ${STAGE} --write" | tee -a "${preflight_log}"
    preflight_rc=5
  fi
fi
if [[ "${preflight_rc}" -eq 0 && "${DRY_RUN}" != "1" && "${REQUIRE_API_KEY}" == "1" &&
      -z "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
  {
    echo "[FAIL] FUZZPILOT_MODEL_API_KEY is required for real architecture stages"
    echo "[FAIL] set REQUIRE_API_KEY=0 only for intentional non-LLM infrastructure drills"
  } | tee -a "${preflight_log}"
  preflight_rc=3
fi

previous_stage=""
case "${STAGE}" in
  B|b) previous_stage="A" ;;
  C|c) previous_stage="B" ;;
esac
if [[ "${preflight_rc}" -eq 0 && "${previous_stage}" != "" &&
      "${REQUIRE_PREVIOUS_STAGE_PASS}" == "1" ]]; then
  previous_report="${RESULT_ROOT}/stage_${previous_stage,,}_acceptance.md"
  previous_status="$(find "${LOG_ROOT}" -maxdepth 1 -type f \
      -name "stage_${previous_stage,,}_*_status.json" 2>/dev/null | sort | tail -1)"
  previous_ok="1"
  if [[ ! -s "${previous_report}" ]]; then
    echo "[FAIL] previous stage ${previous_stage} report missing: ${previous_report}" | tee -a "${preflight_log}"
    previous_ok="0"
  elif grep -q -- '- \[FAIL\]' "${previous_report}"; then
    echo "[FAIL] previous stage ${previous_stage} acceptance report has failed gates: ${previous_report}" | tee -a "${preflight_log}"
    previous_ok="0"
  fi
  if [[ -z "${previous_status}" ]]; then
    echo "[FAIL] previous stage ${previous_stage} wrapper status JSON missing" | tee -a "${preflight_log}"
    previous_ok="0"
  elif ! python3 - "${previous_status}" <<'PY'
import json
import sys
path = sys.argv[1]
with open(path, encoding="utf-8") as inp:
    status = json.load(inp)
ok = (
    int(status.get("preflight_exit_code", 1)) == 0
    and int(status.get("runner_exit_code", 1)) == 0
    and int(status.get("summary_exit_code", 1)) == 0
)
raise SystemExit(0 if ok else 1)
PY
  then
    echo "[FAIL] previous stage ${previous_stage} wrapper status is not clean: ${previous_status}" | tee -a "${preflight_log}"
    previous_ok="0"
  fi
  if [[ "${previous_ok}" != "1" ]]; then
    echo "[FAIL] set REQUIRE_PREVIOUS_STAGE_PASS=0 only for an intentional non-paper drill" | tee -a "${preflight_log}"
    preflight_rc=4
  else
    echo "[PASS] previous stage ${previous_stage} passed acceptance and wrapper status checks" | tee -a "${preflight_log}"
  fi
fi

runner_rc=0
if [[ "${preflight_rc}" -eq 0 ]]; then
  scripts/paper01/architecture_noise_monitor.py \
    --stage "${STAGE}" \
    --jsonl "${noise_log}" \
    --summary "${noise_summary}" \
    --root "${REPO_ROOT}" \
    --interval-sec "${NOISE_MONITOR_INTERVAL_SEC:-30}" \
    --max-load1-per-cpu "${NOISE_MAX_LOAD1_PER_CPU:-1.25}" \
    --min-mem-available-kib "${NOISE_MIN_MEM_AVAILABLE_KIB:-1048576}" \
    --min-disk-free-gib "${NOISE_MIN_DISK_FREE_GIB:-20}" &
  noise_monitor_pid=$!
  echo "[stage] runner ${STAGE}"
  set +e
  DRY_RUN="${DRY_RUN}" scripts/paper01/runners/run_architecture_pilot.sh "${STAGE}" \
    >"${runner_log}" 2>&1
  runner_rc=$?
  set -e
else
  echo "preflight failed; runner not started" >"${runner_log}"
  runner_rc=2
fi
stop_noise_monitor
noise_monitor_pid=""

echo "[stage] summarize ${STAGE}"
summary_rc=0
if [[ -d "${RUN_ROOT}" ]]; then
  summary_args=(
    "${RUN_ROOT}"
    --stage "${STAGE}"
    --csv "${csv_path}"
    --report "${report_path}"
    --noise-summary "${noise_summary}"
    --stage-plan "${stage_plan}"
  )
  if [[ "${FAIL_ON_GATE_FAIL}" == "1" ]]; then
    summary_args+=(--fail-on-gate-fail)
  fi
  set +e
  python3 scripts/paper01/architecture_pilot_summary.py "${summary_args[@]}" \
    >"${summary_log}" 2>&1
  summary_rc=$?
  set -e
else
  echo "run root missing: ${RUN_ROOT}" >"${summary_log}"
  summary_rc=2
fi

finished_at="$(date -u +%Y%m%dT%H%M%SZ)"
python3 - "${status_json}" "${STAGE}" "${started_at}" "${finished_at}" \
  "${preflight_rc}" "${runner_rc}" "${summary_rc}" "${RUN_ROOT}" "${csv_path}" "${report_path}" \
  "${preflight_log}" "${runner_log}" "${summary_log}" "${noise_log}" "${noise_summary}" "${stage_plan}" <<'PY'
import json
import pathlib
import sys

(
    status_path,
    stage,
    started_at,
    finished_at,
    preflight_rc,
    runner_rc,
    summary_rc,
    run_root,
    csv_path,
    report_path,
    preflight_log,
    runner_log,
    summary_log,
    noise_log,
    noise_summary,
    stage_plan,
) = sys.argv[1:17]

payload = {
    "stage": stage,
    "started_at_utc": started_at,
    "finished_at_utc": finished_at,
    "preflight_exit_code": int(preflight_rc),
    "runner_exit_code": int(runner_rc),
    "summary_exit_code": int(summary_rc),
    "run_root": run_root,
    "csv": csv_path,
    "report": report_path,
    "stage_plan": stage_plan,
    "logs": {
        "preflight": preflight_log,
        "runner": runner_log,
        "summary": summary_log,
        "noise": noise_log,
        "noise_summary": noise_summary,
        "stage_plan": stage_plan,
    },
}
pathlib.Path(status_path).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
rm -f "${status_json}.tmp"

echo "[stage] preflight_exit_code=${preflight_rc} runner_exit_code=${runner_rc} summary_exit_code=${summary_rc}"
echo "[stage] status=${status_json}"
echo "[stage] report=${report_path}"

if [[ "${preflight_rc}" -ne 0 ]]; then
  exit "${preflight_rc}"
fi
if [[ "${runner_rc}" -ne 0 ]]; then
  exit "${runner_rc}"
fi
exit "${summary_rc}"
