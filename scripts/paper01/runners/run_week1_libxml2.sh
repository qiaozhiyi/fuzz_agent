#!/usr/bin/env bash
# Paper 01 week-1 focused runner.
#
# Purpose:
#   - Preserve the currently running remote venue-stage results.
#   - Run only the libxml2 closed-loop week-1 matrix locally/remotely:
#       baseline-afl / full-agent / controller-only / rule-only / no-static-analysis
#   - Reuse the existing fuzzpilot CLI and metadata capture path.
#   - Keep raw artifacts by default; no post-run deletion.
#
# Usage:
#   scripts/paper01/runners/run_week1_libxml2.sh [--dry-run] [--from W1b] [--bin PATH]
#
# Notes:
#   - This script is intentionally independent from run_venue_stage1.sh.
#   - It does not touch any existing remote tmux/session orchestration.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MANIFEST="${REPO_ROOT}/experiments/manifests/paper01_week1_libxml2.yaml"
RUNS_ROOT="${REPO_ROOT}/results/paper01_ai_recipe_mutation/runs"
LOG_DIR="${RUNS_ROOT}/_logs"
STATUS="${LOG_DIR}/week1_libxml2.status"
FUZZPILOT_BIN="${FUZZPILOT_BIN:-${REPO_ROOT}/build/fuzzpilot}"
METADATA_SCRIPT="${REPO_ROOT}/scripts/capture_run_metadata.sh"
NCPUS="${FUZZPILOT_PARALLEL:-4}"

mkdir -p "${LOG_DIR}"

DRY_RUN=0
FROM_EXP=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --from) FROM_EXP="${2:-}"; shift 2 ;;
    --bin) FUZZPILOT_BIN="${2:-}"; shift 2 ;;
    -h|--help)
      sed -n '2,24p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ${DRY_RUN} -eq 0 && ! -x "${FUZZPILOT_BIN}" ]]; then
  echo "missing fuzzpilot binary: ${FUZZPILOT_BIN}" >&2
  exit 2
fi

status_set() {
  printf '[%s] %s\n' "$(date -u +%FT%TZ)" "$*" | tee -a "${STATUS}"
}

python_manifest() {
  python3 - "$MANIFEST" "$1" <<'PY'
import json, sys, yaml
manifest = yaml.safe_load(open(sys.argv[1]))
exp_id = sys.argv[2]
defaults = manifest.get("defaults", {})
for exp in manifest["experiments"]:
    if exp["id"] == exp_id:
        if "target_config" not in exp and "target_config" in defaults:
            exp["target_config"] = defaults["target_config"]
        if "budget_sec" not in exp and "budget_sec" in defaults:
            exp["budget_sec"] = defaults["budget_sec"]
        print(json.dumps(exp))
        raise SystemExit(0)
raise SystemExit(1)
PY
}

run_one() {
  local run_id="$1"
  local mode="$2"
  local target_config="$3"
  local target_name="$4"
  local budget_sec="$5"
  local out_dir="${RUNS_ROOT}/${run_id}"
  local status_file="${out_dir}/status"

  mkdir -p "${out_dir}"
  status_set "launch ${run_id} mode=${mode} budget=${budget_sec}s"

  # Fail-loud: any agent-bearing mode needs a real API key. Without this the
  # gateway silently falls back to FakeModelGateway and the LLM is never
  # called — the regression that broke W1b last time.
  case "${mode}" in
    full-agent|rule-only|no-static-analysis|no-microcampaign|no-plateau)
      if [[ -z "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
        status_set "ABORT ${run_id}: FUZZPILOT_MODEL_API_KEY required for mode=${mode}"
        echo "missing-api-key" > "${status_file}"
        return 2
      fi
      ;;
  esac

  if [[ ${DRY_RUN} -eq 1 ]]; then
    echo "DRY: ${FUZZPILOT_BIN} run --real-run --config ${target_config} --ablation ${mode} --main-budget-sec ${budget_sec} --work-dir ${out_dir}/work"
    return 0
  fi

  "${METADATA_SCRIPT}" \
    --run-id "${run_id}" \
    --config "${target_config}" \
    --target "${target_name}" \
    --out-dir "${out_dir}" >/dev/null

  echo "running" > "${status_file}"
  # `stdbuf -oL -eL` forces line-buffered IO so SIGTERM doesn't lose the tail
  # of stderr (where LLM/network failures and stack traces land).
  if stdbuf -oL -eL "${FUZZPILOT_BIN}" run --real-run \
      --config "${target_config}" \
      --ablation "${mode}" \
      --main-budget-sec "${budget_sec}" \
      --work-dir "${out_dir}/work" \
      > "${out_dir}/stdout.log" \
      2> "${out_dir}/stderr.log"; then
    local inner_run
    inner_run="$(find "${out_dir}/work" -maxdepth 1 -type d -name 'run_*' -print -quit || true)"
    if [[ -n "${inner_run}" ]]; then
      for f in coverage.csv events.jsonl agent_decisions.jsonl agent_memory.jsonl fuzzpilot.sqlite main_launch.sh report.md run_metadata.json; do
        [[ -f "${inner_run}/${f}" ]] && cp -f "${inner_run}/${f}" "${out_dir}/${f}"
      done
      [[ -f "${inner_run}/main_out/default/fuzzer_stats" ]] && cp -f "${inner_run}/main_out/default/fuzzer_stats" "${out_dir}/fuzzer_stats"
    fi
    echo "completed" > "${status_file}"
    status_set "complete ${run_id}"
    return 0
  fi

  echo "failed" > "${status_file}"
  status_set "failed ${run_id}"
  return 1
}

run_exp() {
  local exp_id="$1"
  local exp_json
  exp_json="$(python_manifest "${exp_id}")"

  local target_config target_name mode budget
  target_config="$(printf '%s' "${exp_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("target_config",""))')"
  target_name="$(printf '%s' "${exp_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["target"])')"
  mode="$(printf '%s' "${exp_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["mode"])')"
  budget="$(printf '%s' "${exp_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["budget_sec"])')"
  local runs_file
  runs_file="$(mktemp)"
  printf '%s' "${exp_json}" | python3 -c 'import json,sys; [print(x) for x in json.load(sys.stdin)["runs"]]' > "${runs_file}"

  local pids=()
  local fails=0
  while IFS= read -r run_id; do
    while [[ "$(jobs -pr | wc -l | tr -d ' ')" -ge "${NCPUS}" ]]; do
      sleep 2
    done
    run_one "${run_id}" "${mode}" "${REPO_ROOT}/${target_config}" "${target_name}" "${budget}" &
    pids+=($!)
  done < "${runs_file}"
  rm -f "${runs_file}"
  for pid in "${pids[@]}"; do
    if ! wait "${pid}"; then
      fails=$((fails + 1))
    fi
  done
  return "${fails}"
}

EXPS=(W1a W1b W1c W1d W1e)
skip=1
[[ -z "${FROM_EXP}" ]] && skip=0

status_set "week1 libxml2 runner start dry_run=${DRY_RUN} ncpus=${NCPUS}"
for exp in "${EXPS[@]}"; do
  if (( skip )) && [[ "${exp}" != "${FROM_EXP}" ]]; then
    status_set "skip ${exp} (--from filter)"
    continue
  fi
  skip=0
  status_set "exp start ${exp}"
  run_exp "${exp}"
  status_set "exp end ${exp}"
done
status_set "week1 libxml2 runner done"
