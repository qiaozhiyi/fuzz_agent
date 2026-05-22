#!/usr/bin/env bash
# docker/ubuntu/entrypoint.sh
#
# Single entry point for the FuzzPilot Paper 1 Docker image.
#
# Verbs:
#   preflight              — run scripts/paper01/preflight.sh --in-container
#   smoke                  — run preflight, runtime config checks, and a short cJSON AFL++ run
#   run-batch <args...>    — run scripts/paper01/run_batch.sh <args>
#   aggregate <args...>    — run scripts/paper01/aggregate.py <args>
#   plots <args...>        — run scripts/paper01/plots.py <args>
#   all                    — preflight; run-batch E3 E1a E1b E2a E2b E2c; aggregate all; plots all
#   shell                  — drop into bash for debugging
#
# Examples:
#   scripts/fuzzpilot_docker.sh preflight
#   scripts/fuzzpilot_docker.sh smoke
#   scripts/fuzzpilot_docker.sh run-batch --exp E1a --parallel 4
#   scripts/fuzzpilot_docker.sh all

set -euo pipefail

REPO_ROOT="/work/fuzz_agent"
cd "${REPO_ROOT}"

copy_run_artifacts() {
  local work_dir="$1"
  local out_dir="$2"
  local inner_run=""

  inner_run="$(find "${work_dir}" -maxdepth 1 -type d -name 'run_*' -print -quit 2>/dev/null || true)"
  if [[ -z "${inner_run}" ]]; then
    return 0
  fi

  for f in coverage.csv events.jsonl agent_decisions.jsonl agent_memory.jsonl fuzzpilot.sqlite main_launch.sh report.md; do
    if [[ -f "${inner_run}/${f}" ]]; then
      cp -f "${inner_run}/${f}" "${out_dir}/${f}"
    fi
  done
  # AFL++ writes fuzzer_stats two levels deep under main_out/default/.
  local afl_stats="${inner_run}/main_out/default/fuzzer_stats"
  if [[ -f "${afl_stats}" ]]; then
    cp -f "${afl_stats}" "${out_dir}/fuzzer_stats"
  fi
}

run_smoke() {
  local platform="${FUZZPILOT_IMAGE_PLATFORM:-$(uname -s)/$(uname -m)}"
  local arch_safe
  arch_safe="$(printf '%s' "${platform}" | tr '/:' '__')"
  local run_id="docker_smoke_${arch_safe}"
  local out_dir="${REPO_ROOT}/results/docker_smoke/runs/${run_id}"
  local budget="${FUZZPILOT_SMOKE_BUDGET_SEC:-5}"
  local micro_budget="${FUZZPILOT_SMOKE_MICRO_BUDGET_SEC:-1}"
  if (( budget < 2 )); then
    budget=2
  fi
  if (( micro_budget >= budget )); then
    micro_budget=$((budget - 1))
  fi
  if (( micro_budget < 1 )); then
    micro_budget=1
  fi
  local timeout_sec=$((budget + 30))

  bash scripts/paper01/preflight.sh --in-container

  for config in \
    experiments/targets/vuln_target/config.yaml \
    experiments/targets/cjson/config.yaml \
    experiments/targets/libpng/config.yaml; do
    ./build/fuzzpilot check-config --config "${config}" --runtime
  done

  mkdir -p "${out_dir}"
  : > "${out_dir}/stdout.log"
  : > "${out_dir}/stderr.log"
  echo "running" > "${out_dir}/status"

  if ! scripts/capture_run_metadata.sh \
      --run-id "${run_id}" \
      --config experiments/targets/cjson/config.yaml \
      --target cjson_parser \
      --out-dir "${out_dir}" >>"${out_dir}/stdout.log" 2>>"${out_dir}/stderr.log"; then
    echo "failed" > "${out_dir}/status"
    echo "metadata capture failed; see ${out_dir}/stderr.log" >&2
    return 1
  fi

  set +e
  timeout --foreground "${timeout_sec}s" ./build/fuzzpilot run --real-run \
      --config experiments/targets/cjson/config.yaml \
      --ablation baseline-afl \
      --main-budget-sec "${budget}" \
      --micro-budget-sec "${micro_budget}" \
      --work-dir "${out_dir}/work" \
      >> "${out_dir}/stdout.log" 2>> "${out_dir}/stderr.log"
  local rc=$?
  set -e

  copy_run_artifacts "${out_dir}/work" "${out_dir}"
  if [[ ${rc} -eq 0 ]]; then
    echo "completed" > "${out_dir}/status"
    echo "smoke completed: ${out_dir}"
  else
    echo "failed" > "${out_dir}/status"
    echo "smoke failed rc=${rc}: ${out_dir}" >&2
    return "${rc}"
  fi
}

VERB="${1:-preflight}"
shift || true

case "${VERB}" in
  preflight)
    exec bash scripts/paper01/preflight.sh --in-container "$@"
    ;;
  smoke)
    run_smoke "$@"
    ;;
  run-batch|run|batch)
    exec bash scripts/paper01/run_batch.sh "$@"
    ;;
  aggregate|aggr)
    exec python3 scripts/paper01/aggregate.py "$@"
    ;;
  plots|plot)
    exec python3 scripts/paper01/plots.py "$@"
    ;;
  all)
    bash scripts/paper01/preflight.sh --in-container
    for EXP in E3 E1a E1b E2a E2b E2c; do
      bash scripts/paper01/run_batch.sh --exp "${EXP}" --parallel "${FUZZPILOT_PARALLEL:-4}" --resume
    done
    python3 scripts/paper01/aggregate.py all
    python3 scripts/paper01/plots.py all
    ;;
  shell|bash)
    exec /bin/bash "$@"
    ;;
  --help|-h|help)
    sed -n '3,20p' "$0"
    ;;
  *)
    echo "unknown verb: ${VERB}" >&2
    sed -n '3,20p' "$0" >&2
    exit 2
    ;;
esac
