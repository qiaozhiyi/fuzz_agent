#!/usr/bin/env bash
# scripts/paper01/run_batch.sh
#
# Paper 1 batch driver. Reads experiments/manifests/paper01_preprint.yaml
# and launches every run in a given experiment ID.
#
# Usage:
#   scripts/paper01/run_batch.sh --exp E1a [--repeats N] [--budget-sec N]
#                                [--parallel J] [--dry-run] [--resume]
#
# Each run produces:
#   results/paper01_ai_recipe_mutation/runs/<run_id>/
#     run_metadata.json
#     work/                          (fuzzpilot --work-dir output)
#     fuzzer_stats coverage.csv events.jsonl  (copied/linked from work/)
#     agent_decisions.jsonl agent_memory.jsonl fuzzpilot.sqlite  (full-agent only)
#     main_launch.sh
#     status                          (one of: completed, failed, failed_short_run, skipped)
#     stdout.log stderr.log
#
# Exit codes:
#   0  all requested runs completed
#   1  one or more runs failed
#   2  manifest / preflight problem

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="${REPO_ROOT}/experiments/manifests/paper01_preprint.yaml"
RESULTS_ROOT="${REPO_ROOT}/results/paper01_ai_recipe_mutation"
RUNS_ROOT="${RESULTS_ROOT}/runs"
MICRO_ROOT="${RESULTS_ROOT}/microbench"
METADATA_SCRIPT="${REPO_ROOT}/scripts/capture_run_metadata.sh"

# Locate fuzzpilot and microbench binaries.
# Honor env overrides first; otherwise try build/ (Docker convention) then
# build-cmake/ (native CMake-out-of-source convention).
# Always returns 0 (caller inspects the result; empty == not found).
find_binary() {
  local override="$1"
  shift
  if [[ -n "${override}" ]]; then
    if [[ -x "${override}" ]]; then
      echo "${override}"
    else
      echo ""
    fi
    return 0
  fi
  for candidate in "$@"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  echo ""
  return 0
}

FUZZPILOT_BIN_OVERRIDE="${FUZZPILOT_BIN:-}"
MICROBENCH_BIN_OVERRIDE="${MICROBENCH_BIN:-}"

FUZZPILOT_BIN="$(find_binary "${FUZZPILOT_BIN_OVERRIDE}" \
  "${REPO_ROOT}/build/fuzzpilot" \
  "${REPO_ROOT}/build-cmake/fuzzpilot")"
MICROBENCH_BIN="$(find_binary "${MICROBENCH_BIN_OVERRIDE}" \
  "${REPO_ROOT}/build/tools/mutator_microbench/mutator_microbench" \
  "${REPO_ROOT}/build-cmake/tools/mutator_microbench/mutator_microbench")"

EXP=""
REPEATS_OVERRIDE=""
BUDGET_OVERRIDE=""
PARALLEL=1
DRY_RUN=0
RESUME=0

usage() {
  sed -n '3,25p' "${BASH_SOURCE[0]}"
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exp)          EXP="$2"; shift 2 ;;
    --repeats)      REPEATS_OVERRIDE="$2"; shift 2 ;;
    --budget-sec)   BUDGET_OVERRIDE="$2"; shift 2 ;;
    --parallel)     PARALLEL="$2"; shift 2 ;;
    --dry-run)      DRY_RUN=1; shift ;;
    --resume)       RESUME=1; shift ;;
    -h|--help)      usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -z "${EXP}" ]] && usage
[[ -f "${MANIFEST}" ]] || { echo "manifest missing: ${MANIFEST}" >&2; exit 2; }
if [[ ${DRY_RUN} -eq 0 ]]; then
  if [[ -z "${FUZZPILOT_BIN}" ]]; then
    echo "fuzzpilot binary not found. Tried (in order):" >&2
    echo "  \$FUZZPILOT_BIN env: ${FUZZPILOT_BIN_OVERRIDE:-<unset>}" >&2
    echo "  ${REPO_ROOT}/build/fuzzpilot" >&2
    echo "  ${REPO_ROOT}/build-cmake/fuzzpilot" >&2
    echo "Build it (cmake --build build-cmake --target fuzzpilot) or set FUZZPILOT_BIN." >&2
    exit 2
  fi
fi

mkdir -p "${RUNS_ROOT}" "${MICRO_ROOT}"

# We avoid a hard yaml dependency: a tiny python parser does just enough.
python_yaml() {
  python3 - "$@" <<'PY'
import sys, yaml, json, os
manifest = yaml.safe_load(open(sys.argv[1]))
exp_id = sys.argv[2]
for e in manifest["experiments"]:
    if e["id"] == exp_id:
        print(json.dumps(e))
        sys.exit(0)
print(json.dumps(None))
PY
}

EXP_JSON="$(python_yaml "${MANIFEST}" "${EXP}")"
if [[ "${EXP_JSON}" == "null" ]]; then
  echo "experiment ${EXP} not in manifest" >&2; exit 2
fi

KIND="$(echo "${EXP_JSON}" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("kind","afl"))')"

run_afl_one() {
  local run_id="$1"
  local mode="$2"
  local target_config="$3"
  local budget="$4"
  local out_dir="${RUNS_ROOT}/${run_id}"
  local status_file="${out_dir}/status"

  if [[ ${RESUME} -eq 1 && -f "${status_file}" && "$(cat "${status_file}")" == "completed" ]]; then
    echo "[${run_id}] resume: already completed, skipping"
    return 0
  fi

  if [[ ${DRY_RUN} -eq 1 ]]; then
    # Dry-run is side-effect-free: announce only, do not touch status, logs,
    # the work directory, or metadata. This lets `--dry-run` be used as a
    # safe plumbing check even on a results dir with real run state.
    mkdir -p "${out_dir}"
    echo "[${run_id}] DRY-RUN mode=${mode} budget=${budget}s out=${out_dir}"
    echo "  DRY-RUN: ${FUZZPILOT_BIN} run --real-run --config ${target_config} --ablation ${mode} --main-budget-sec ${budget} --work-dir ${out_dir}/work"
    return 0
  fi

  mkdir -p "${out_dir}"
  : > "${out_dir}/stdout.log"
  : > "${out_dir}/stderr.log"
  echo "running" > "${status_file}"

  echo "[${run_id}] mode=${mode} budget=${budget}s out=${out_dir}"

  # Capture pre-run metadata
  if ! "${METADATA_SCRIPT}" \
      --run-id "${run_id}" \
      --config "${target_config}" \
      --target "$(echo "${EXP_JSON}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["target"])')" \
      --out-dir "${out_dir}" >> "${out_dir}/stdout.log" 2>> "${out_dir}/stderr.log"; then
    echo "failed" > "${status_file}"
    echo "[${run_id}] FAILED during metadata capture; see ${out_dir}/stderr.log" >&2
    return 1
  fi

  set +e
  # Filter AFL's per-test-case progress noise on the way to stdout.log.
  # AFL prints ~1.2M "Fuzzing test case #N (M total, ...)" lines over 4h, which
  # bloats stdout.log to ~250-700 MB while contributing zero paper-relevant
  # signal (cumulative state is in fuzzer_stats / coverage.csv; WARNING / ERROR /
  # banner / summary lines do NOT match this pattern, so they pass through).
  # grep --line-buffered avoids buffering 4h of output in RAM.
  "${FUZZPILOT_BIN}" run --real-run \
      --config "${target_config}" \
      --ablation "${mode}" \
      --main-budget-sec "${budget}" \
      --work-dir "${out_dir}/work" \
      2> "${out_dir}/stderr.log" \
      | grep --line-buffered -vF "Fuzzing test case" > "${out_dir}/stdout.log"
  local rc=${PIPESTATUS[0]}
  set -e

  # Surface the most recent run_<id> subdir into the top level for easy access.
  # Most artifacts live directly under inner_run, but AFL++ writes fuzzer_stats
  # into inner_run/main_out/default/, so copy that one from its real location.
  local inner_run
  inner_run="$(find "${out_dir}/work" -maxdepth 1 -type d -name 'run_*' -print -quit || true)"
  if [[ -n "${inner_run}" ]]; then
    for f in coverage.csv events.jsonl agent_decisions.jsonl agent_memory.jsonl fuzzpilot.sqlite main_launch.sh report.md; do
      [[ -f "${inner_run}/${f}" ]] && cp -f "${inner_run}/${f}" "${out_dir}/${f}"
    done
    local afl_stats="${inner_run}/main_out/default/fuzzer_stats"
    [[ -f "${afl_stats}" ]] && cp -f "${afl_stats}" "${out_dir}/fuzzer_stats"
  fi

  if [[ ${rc} -eq 0 ]]; then
    # Runtime gate: even if AFL exited cleanly, reject the run if it didn't
    # actually spend close to its budget. A 120s "completed" run with millions
    # of execs against a small seed corpus would otherwise look fine to the
    # min_execs_done gate but isn't a paper-grade 4-hour baseline.
    local min_run_time
    min_run_time="$(python3 - "${MANIFEST}" <<'PY'
import sys, yaml
m = yaml.safe_load(open(sys.argv[1]))
print(int(m.get("acceptance", {}).get("min_run_time_sec", 0)))
PY
)"
    local observed_run_time=""
    if [[ -f "${out_dir}/fuzzer_stats" ]]; then
      observed_run_time="$(awk -F: '/^run_time/ {gsub(/ /, "", $2); print $2; exit}' "${out_dir}/fuzzer_stats")"
    fi
    if [[ -n "${min_run_time}" && "${min_run_time}" -gt 0 && -n "${observed_run_time}" && "${observed_run_time}" -lt "${min_run_time}" ]]; then
      echo "failed-short-run" > "${status_file}"
      echo "[${run_id}] FAILED: run_time=${observed_run_time}s < min_run_time_sec=${min_run_time}s (re-run with --resume to retry)" >&2
      return 1
    fi
    # Active-agent gates: full-agent runs must produce >= N plateau events and
    # >= N agent decisions. A full-agent run with zero of either is a silent
    # failure (mutator loaded, LLM never called). Without these gates the W1b
    # regression pattern (mode statistically equal to baseline-afl because
    # the agent never triggered) merges silently.
    if [[ "${mode}" == "full-agent" ]]; then
      local min_plateau min_decisions
      read -r min_plateau min_decisions < <(python3 - "${MANIFEST}" <<'PY'
import sys, yaml
a = (yaml.safe_load(open(sys.argv[1])) or {}).get("acceptance", {}) or {}
print(int(a.get("per_full_agent_min_plateau_events", 0)),
      int(a.get("per_full_agent_min_agent_decisions", 0)))
PY
)
      local observed_plateau=0 observed_decisions=0
      if [[ -f "${out_dir}/events.jsonl" ]]; then
        observed_plateau=$(grep -c '"event":"plateau_detected"' "${out_dir}/events.jsonl" 2>/dev/null || echo 0)
      fi
      if [[ -f "${out_dir}/agent_decisions.jsonl" ]]; then
        observed_decisions=$(wc -l < "${out_dir}/agent_decisions.jsonl" | tr -d '[:space:]')
      fi
      if [[ "${min_plateau}" -gt 0 && "${observed_plateau:-0}" -lt "${min_plateau}" ]]; then
        echo "failed" > "${status_file}"
        echo "${rc}" > "${out_dir}/exit_code"
        echo "[${run_id}] FAILED: plateau_events=${observed_plateau} < per_full_agent_min_plateau_events=${min_plateau} (agent never triggered)" >&2
        return 1
      fi
      if [[ "${min_decisions}" -gt 0 && "${observed_decisions:-0}" -lt "${min_decisions}" ]]; then
        echo "failed" > "${status_file}"
        echo "${rc}" > "${out_dir}/exit_code"
        echo "[${run_id}] FAILED: agent_decisions=${observed_decisions} < per_full_agent_min_agent_decisions=${min_decisions} (LLM never called)" >&2
        return 1
      fi
    fi
    echo "completed" > "${status_file}"
    echo "[${run_id}] completed (run_time=${observed_run_time:-?}s)"
    return 0
  else
    echo "failed" > "${status_file}"
    echo "${rc}" > "${out_dir}/exit_code"
    echo "[${run_id}] FAILED (rc=${rc}); see ${out_dir}/stderr.log" >&2
    return 1
  fi
}

run_microbench_one() {
  local run_id="$1"
  local config_name="$2"   # vanilla | fp-empty | fp-active
  local iters="$3"
  local seed_count="$4"
  local out_json="${MICRO_ROOT}/${run_id}.json"
  local status_file="${MICRO_ROOT}/${run_id}.status"

  if [[ ${RESUME} -eq 1 && -f "${status_file}" && "$(cat "${status_file}")" == "completed" ]]; then
    echo "[${run_id}] resume: already completed, skipping"
    return 0
  fi

  if [[ -z "${MICROBENCH_BIN}" && ${DRY_RUN} -eq 0 ]]; then
    echo "microbench binary not found. Tried (in order):" >&2
    echo "  \$MICROBENCH_BIN env: ${MICROBENCH_BIN_OVERRIDE:-<unset>}" >&2
    echo "  ${REPO_ROOT}/build/tools/mutator_microbench/mutator_microbench" >&2
    echo "  ${REPO_ROOT}/build-cmake/tools/mutator_microbench/mutator_microbench" >&2
    echo "Build it (cmake --build build-cmake --target mutator_microbench) or set MICROBENCH_BIN." >&2
    return 1
  fi

  echo "[${run_id}] config=${config_name} iters=${iters}"
  if [[ ${DRY_RUN} -eq 1 ]]; then
    echo "  DRY-RUN: ${MICROBENCH_BIN} --config ${config_name} --seeds experiments/targets/cjson/seeds --iterations ${iters} --seed-count ${seed_count} --out ${out_json}"
    # Do not mutate ${status_file} during a dry-run.
    return 0
  fi

  set +e
  "${MICROBENCH_BIN}" \
      --config "${config_name}" \
      --seeds "${REPO_ROOT}/experiments/targets/cjson/seeds" \
      --iterations "${iters}" \
      --seed-count "${seed_count}" \
      --out "${out_json}"
  local rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    echo "completed" > "${status_file}"
    return 0
  else
    echo "failed" > "${status_file}"
    return 1
  fi
}

# Build the queue
QUEUE_FILE="$(mktemp)"
trap 'rm -f "${QUEUE_FILE}"' EXIT

if [[ "${KIND}" == "microbench" ]]; then
  python3 - "${EXP_JSON}" "${QUEUE_FILE}" <<'PY'
import json, sys
exp = json.loads(sys.argv[1])
out = open(sys.argv[2], 'w')
iters = exp["iterations"]
seed_count = exp.get("seed_count", 10000)
for run_id in exp["runs"]:
    # run_id is like p1_e3_microbench_<config>_r<NN>
    parts = run_id.split('_')
    cfg = parts[3]
    out.write(f"MICRO\t{run_id}\t{cfg}\t{iters}\t{seed_count}\n")
PY
else
  python3 - "${EXP_JSON}" "${QUEUE_FILE}" "${BUDGET_OVERRIDE}" "${REPEATS_OVERRIDE}" "${REPO_ROOT}" <<'PY'
import json, sys, os
exp = json.loads(sys.argv[1])
out = open(sys.argv[2], 'w')
budget_override = sys.argv[3]
repeats_override = sys.argv[4]
repo_root = sys.argv[5]

budget = int(budget_override) if budget_override else exp["budget_sec"]
target_config = exp.get("target_config", "experiments/targets/cjson/config.yaml")
target_config_abs = os.path.join(repo_root, target_config) if not os.path.isabs(target_config) else target_config

runs = exp["runs"]
if repeats_override:
    runs = runs[:int(repeats_override)]

mode = exp.get("mode")  # may be None for E5 which carries per-run mode

for r in runs:
    if isinstance(r, dict):
        rid = r["id"]
        m = r.get("mode", mode)
    else:
        rid = r
        m = mode
    out.write(f"AFL\t{rid}\t{m}\t{target_config_abs}\t{budget}\n")
PY
fi

# Execute queue
FAILS=0
TOTAL=0
LINES=()
while IFS= read -r line; do
  LINES+=("$line")
done < "${QUEUE_FILE}"
TOTAL=${#LINES[@]}
echo "queue: ${TOTAL} runs (parallel=${PARALLEL})"

# Simple semaphore for parallelism
SEM=$(mktemp -d)
trap 'rm -rf "${SEM}"; rm -f "${QUEUE_FILE}"' EXIT
for ((i=0; i<PARALLEL; i++)); do : > "${SEM}/slot_${i}"; done

acquire_slot() {
  while :; do
    for ((i=0; i<PARALLEL; i++)); do
      if [[ -f "${SEM}/slot_${i}" ]]; then
        mv "${SEM}/slot_${i}" "${SEM}/busy_${i}.$$" 2>/dev/null && { echo $i; return; }
      fi
    done
    sleep 2
  done
}
release_slot() {
  local i="$1"
  mv "${SEM}/busy_${i}.$$" "${SEM}/slot_${i}" 2>/dev/null || true
}

PIDS=()
for line in "${LINES[@]}"; do
  IFS=$'\t' read -ra parts <<< "${line}"
  kind="${parts[0]}"
  slot="$(acquire_slot)"
  (
    if [[ "${kind}" == "AFL" ]]; then
      run_afl_one "${parts[1]}" "${parts[2]}" "${parts[3]}" "${parts[4]}"
    else
      run_microbench_one "${parts[1]}" "${parts[2]}" "${parts[3]}" "${parts[4]}"
    fi
    rc=$?
    release_slot "${slot}"
    exit $rc
  ) &
  PIDS+=($!)
done

for pid in "${PIDS[@]}"; do
  if ! wait "$pid"; then
    FAILS=$((FAILS+1))
  fi
done

echo "batch done: total=${TOTAL} failed=${FAILS}"
[[ ${FAILS} -eq 0 ]] || exit 1
exit 0
