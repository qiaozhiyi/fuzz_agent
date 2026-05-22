#!/usr/bin/env bash
# scripts/paper01/run_e1a_e2a.sh
#
# 一键脚本：把 Paper 1 中**不需要 API key** 的两个实验从头跑到尾。
#
#   Phase 1  preflight        容器内环境自检
#   Phase 2  E1a baseline-afl 5 × 4h，并行 4，纯 AFL++
#   Phase 3  E2a rule-only    3 × 4h，并行 3，FuzzPilot + 本地规则（无 LLM）
#   Phase 4  aggregate        刷新 T1 / coverage_summary / overhead / validity_report
#
# 默认顺序执行，总 wall-time 估算 ~9h（E1a 5h + E2a 4h）。
# 所有底层批处理均带 --resume，崩了重跑该脚本会从断点续上。
#
# Usage:
#   scripts/paper01/run_e1a_e2a.sh                # 前台跑，看实时日志
#   nohup scripts/paper01/run_e1a_e2a.sh > out.log 2>&1 &   # 后台跑
#   scripts/paper01/run_e1a_e2a.sh --dry-run      # 只检查 plumbing，不真跑
#   scripts/paper01/run_e1a_e2a.sh --only E1a     # 只跑 E1a
#   scripts/paper01/run_e1a_e2a.sh --only E2a     # 只跑 E2a
#   scripts/paper01/run_e1a_e2a.sh --skip-preflight
#   scripts/paper01/run_e1a_e2a.sh --skip-aggregate
#
# Exit codes:
#   0  全部成功
#   1  某 phase 失败（看日志末尾即可定位）
#   2  参数 / 环境问题

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

# --- defaults
: "${FUZZPILOT_DOCKER_PLATFORM:=linux/arm64}"
export FUZZPILOT_DOCKER_PLATFORM

E1A_PARALLEL=4
E2A_PARALLEL=3
ONLY=""
DRY_RUN=0
SKIP_PREFLIGHT=0
SKIP_AGGREGATE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)         DRY_RUN=1; shift ;;
    --only)            ONLY="$2"; shift 2 ;;
    --skip-preflight)  SKIP_PREFLIGHT=1; shift ;;
    --skip-aggregate)  SKIP_AGGREGATE=1; shift ;;
    --e1a-parallel)    E1A_PARALLEL="$2"; shift 2 ;;
    --e2a-parallel)    E2A_PARALLEL="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,30p' "${BASH_SOURCE[0]}"
      exit 0 ;;
    *)
      echo "ERROR: unknown arg '$1'" >&2
      sed -n '2,30p' "${BASH_SOURCE[0]}" >&2
      exit 2 ;;
  esac
done

# --- master log
LOG_DIR="${REPO_ROOT}/results/paper01_ai_recipe_mutation/runs/_logs"
mkdir -p "${LOG_DIR}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
MASTER_LOG="${LOG_DIR}/run_e1a_e2a_${TIMESTAMP}.log"

# tee everything to master log
exec > >(tee -a "${MASTER_LOG}") 2>&1

ts() { date -u +"%H:%M:%SZ"; }
log() { echo "[$(ts)] $*"; }
hr()  { printf '%s\n' "----------------------------------------------------------------"; }

# --- preconditions
need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { log "ERROR: '$1' not on PATH"; exit 2; }
}
need_cmd docker
need_cmd python3
[[ -x "${REPO_ROOT}/scripts/fuzzpilot_docker.sh" ]] || {
  log "ERROR: scripts/fuzzpilot_docker.sh not executable"; exit 2;
}

if ! docker ps >/dev/null 2>&1; then
  log "ERROR: docker daemon not running (try opening Docker Desktop)"
  exit 2
fi

hr
log "Paper 1 — E1a + E2a one-button runner"
log "platform:        ${FUZZPILOT_DOCKER_PLATFORM}"
log "e1a parallel:    ${E1A_PARALLEL}"
log "e2a parallel:    ${E2A_PARALLEL}"
log "dry-run:         ${DRY_RUN}"
log "only:            ${ONLY:-<both>}"
log "master log:      ${MASTER_LOG}"
hr

# 总 wall-time 估算
budget_sec_per_run=14400
total_runs=0
case "${ONLY}" in
  E1a) eff_e1a=$(( E1A_PARALLEL < 5 ? E1A_PARALLEL : 5 )); wall=$(( (5 * budget_sec_per_run + eff_e1a - 1) / eff_e1a )) ;;
  E2a) eff_e2a=$(( E2A_PARALLEL < 3 ? E2A_PARALLEL : 3 )); wall=$(( (3 * budget_sec_per_run + eff_e2a - 1) / eff_e2a )) ;;
  *)
    eff_e1a=$(( E1A_PARALLEL < 5 ? E1A_PARALLEL : 5 ))
    eff_e2a=$(( E2A_PARALLEL < 3 ? E2A_PARALLEL : 3 ))
    w1=$(( (5 * budget_sec_per_run + eff_e1a - 1) / eff_e1a ))
    w2=$(( (3 * budget_sec_per_run + eff_e2a - 1) / eff_e2a ))
    wall=$(( w1 + w2 ))
    ;;
esac
wall_h=$(( wall / 3600 ))
wall_m=$(( (wall % 3600) / 60 ))
log "estimated wall-clock: ~${wall_h}h${wall_m}m"
hr

# --- phase 1: preflight
if [[ ${SKIP_PREFLIGHT} -eq 0 ]]; then
  log "PHASE 1/4: in-container preflight"
  if ! scripts/fuzzpilot_docker.sh preflight; then
    log "ERROR: preflight failed; fix the above and re-run"
    exit 1
  fi
  log "PHASE 1/4: PASS"
  hr
else
  log "PHASE 1/4: SKIPPED"
  hr
fi

run_batch() {
  local exp="$1"
  local par="$2"
  local args=(run-batch --exp "${exp}" --resume --parallel "${par}")
  [[ ${DRY_RUN} -eq 1 ]] && args+=(--dry-run)
  log "command: scripts/fuzzpilot_docker.sh ${args[*]}"
  scripts/fuzzpilot_docker.sh "${args[@]}"
}

# --- phase 2: E1a
if [[ -z "${ONLY}" || "${ONLY}" == "E1a" ]]; then
  log "PHASE 2/4: E1a baseline-afl × 5 (parallel ${E1A_PARALLEL})"
  log "  mode: baseline-afl (pure AFL++ — no LLM, no mutator, no static, no micro)"
  log "  output: results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_baseline-afl_r0{1..5}/"
  if ! run_batch E1a "${E1A_PARALLEL}"; then
    log "ERROR: E1a batch failed (see ${MASTER_LOG})"
    exit 1
  fi
  log "PHASE 2/4: E1a complete"
  hr
else
  log "PHASE 2/4: E1a SKIPPED (--only ${ONLY})"
  hr
fi

# --- phase 3: E2a
if [[ -z "${ONLY}" || "${ONLY}" == "E2a" ]]; then
  log "PHASE 3/4: E2a rule-only × 3 (parallel ${E2A_PARALLEL})"
  log "  mode: rule-only (FuzzPilot loop + recipe mutator, LLM disabled)"
  log "  output: results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r0{1..3}/"
  if ! run_batch E2a "${E2A_PARALLEL}"; then
    log "ERROR: E2a batch failed (see ${MASTER_LOG})"
    exit 1
  fi
  log "PHASE 3/4: E2a complete"
  hr
else
  log "PHASE 3/4: E2a SKIPPED (--only ${ONLY})"
  hr
fi

# --- phase 4: aggregate
if [[ ${SKIP_AGGREGATE} -eq 0 && ${DRY_RUN} -eq 0 ]]; then
  log "PHASE 4/4: aggregate + paper tables (host-side python)"
  if ! python3 scripts/paper01/aggregate.py all; then
    log "WARN: aggregate.py failed (non-fatal); see ${MASTER_LOG}"
  fi
  if ! python3 scripts/prepare_paper01_data.py \
        --run-root results/paper01_ai_recipe_mutation/runs \
        --out-dir  results/paper01_ai_recipe_mutation \
        --manifest experiments/manifests/paper01_preprint.yaml; then
    log "WARN: prepare_paper01_data.py failed (non-fatal); see ${MASTER_LOG}"
  fi
  log "PHASE 4/4: done"
  log ""
  log "Key outputs:"
  log "  results/paper01_ai_recipe_mutation/validity_report.md"
  log "  results/paper01_ai_recipe_mutation/tables/T1_per_run_summary.md"
  log "  results/paper01_ai_recipe_mutation/tables/throughput_parity.md"
  log "  results/paper01_ai_recipe_mutation/tables/coverage_summary.csv"
  log "  results/paper01_ai_recipe_mutation/tables/overhead.csv"
  hr
else
  log "PHASE 4/4: aggregation SKIPPED"
  hr
fi

log "DONE — all phases succeeded."
exit 0
