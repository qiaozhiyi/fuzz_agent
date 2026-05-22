#!/usr/bin/env bash
# scripts/paper01/run_all_host.sh
#
# 一键 host 模式跑全部 Paper 1 实验，最后 aggregate + paper tables。
#
#   Phase 1  preflight --host
#   Phase 2..N  for EXP in E3 E1a E1b E2a E2b E2c: run_batch.sh --exp $EXP --resume
#   Phase aggregate  aggregate.py all + prepare_paper01_data.py
#
# 默认序列对齐 docker entrypoint.sh 的 `all` verb（先 E3 microbench，再 E1/E2 套件）。
# 所有底层 run_batch 都带 --resume，崩了重跑可断点续上。
#
# 未设 FUZZPILOT_MODEL_API_KEY 时，自动跳过需要 LLM 的 full-agent 系实验（E1b/E2b/E2c），
# 不让整个流程因 key 缺失而 abort。
#
# Usage:
#   scripts/paper01/run_all_host.sh                       # 跑全部能跑的实验
#   scripts/paper01/run_all_host.sh --parallel 2          # 控制并行度（默认 4）
#   scripts/paper01/run_all_host.sh --only E1a,E2a        # 只跑指定实验（逗号分隔）
#   scripts/paper01/run_all_host.sh --skip E1b,E2b,E2c    # 跳过指定实验
#   scripts/paper01/run_all_host.sh --skip-preflight
#   scripts/paper01/run_all_host.sh --skip-aggregate
#   scripts/paper01/run_all_host.sh --dry-run             # 不真跑，只验证 plumbing
#
# Exit codes:
#   0  全部成功（或所有需要的 exp 都跑完了 — 含 API key 缺失导致的跳过）
#   1  某 exp 批处理失败（其他 exp 仍会尝试跑；总有失败时退 1）
#   2  preflight / 参数 / 环境问题

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

# --- defaults
ALL_EXPS=(E3 E1a E1b E2a E2b E2c)
FULL_AGENT_EXPS=(E1b E2b E2c)   # need FUZZPILOT_MODEL_API_KEY
PARALLEL="${FUZZPILOT_PARALLEL:-4}"
ONLY=""
SKIP=""
SKIP_PREFLIGHT=0
SKIP_AGGREGATE=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --parallel)        PARALLEL="$2"; shift 2 ;;
    --only)            ONLY="$2"; shift 2 ;;
    --skip)            SKIP="$2"; shift 2 ;;
    --skip-preflight)  SKIP_PREFLIGHT=1; shift ;;
    --skip-aggregate)  SKIP_AGGREGATE=1; shift ;;
    --dry-run)         DRY_RUN=1; shift ;;
    -h|--help)         sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "ERROR: unknown arg '$1'" >&2; exit 2 ;;
  esac
done

# --- master log
LOG_DIR="${REPO_ROOT}/results/paper01_ai_recipe_mutation/runs/_logs"
mkdir -p "${LOG_DIR}"
TS="$(date -u +%Y%m%dT%H%M%SZ)"
MASTER_LOG="${LOG_DIR}/run_all_host_${TS}.log"
exec > >(tee -a "${MASTER_LOG}") 2>&1

ts()  { date -u +"%H:%M:%SZ"; }
log() { echo "[$(ts)] $*"; }
hr()  { printf '%s\n' "----------------------------------------------------------------"; }

# --- preconditions
need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { log "ERROR: '$1' not on PATH"; exit 2; }
}
need_cmd python3
need_cmd afl-fuzz
if [[ ! -x "${REPO_ROOT}/build/fuzzpilot" && ! -x "${REPO_ROOT}/build-cmake/fuzzpilot" ]]; then
  log "ERROR: build/fuzzpilot not found (build with: cmake -S . -B build -G Ninja && cmake --build build)"
  exit 2
fi

# --- assemble experiment list (ONLY takes precedence over SKIP)
in_list() {
  local needle="$1"; shift
  for x in "$@"; do [[ "$x" == "$needle" ]] && return 0; done
  return 1
}

declare -a EXPS
if [[ -n "${ONLY}" ]]; then
  IFS=',' read -ra _only <<< "${ONLY}"
  for e in "${_only[@]}"; do
    if ! in_list "$e" "${ALL_EXPS[@]}"; then
      log "ERROR: --only '$e' not in known list (${ALL_EXPS[*]})"
      exit 2
    fi
    EXPS+=("$e")
  done
else
  EXPS=("${ALL_EXPS[@]}")
fi

if [[ -n "${SKIP}" ]]; then
  IFS=',' read -ra _skip <<< "${SKIP}"
  declare -a _filt
  for e in "${EXPS[@]}"; do
    if ! in_list "$e" "${_skip[@]}"; then _filt+=("$e"); fi
  done
  EXPS=("${_filt[@]}")
fi

# --- API key gate: drop full-agent exps if FUZZPILOT_MODEL_API_KEY is unset
if [[ -z "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
  declare -a _kept _dropped
  for e in "${EXPS[@]}"; do
    if in_list "$e" "${FULL_AGENT_EXPS[@]}"; then
      _dropped+=("$e")
    else
      _kept+=("$e")
    fi
  done
  if (( ${#_dropped[@]} > 0 )); then
    log "WARN: FUZZPILOT_MODEL_API_KEY unset — skipping full-agent exps: ${_dropped[*]}"
    log "      (export FUZZPILOT_MODEL_API_KEY=... and re-run to include them)"
  fi
  EXPS=("${_kept[@]}")
fi

if (( ${#EXPS[@]} == 0 )); then
  log "ERROR: no experiments to run after applying --only/--skip and API-key filter"
  exit 2
fi

hr
log "Paper 1 — run-all on host"
log "exps:       ${EXPS[*]}"
log "parallel:   ${PARALLEL}"
log "dry-run:    ${DRY_RUN}"
log "skip-pre:   ${SKIP_PREFLIGHT}"
log "skip-aggr:  ${SKIP_AGGREGATE}"
log "master log: ${MASTER_LOG}"
hr

# --- wall-clock estimate (AFL exps only; microbench finishes in minutes)
budget_sec=14400
afl_runs_per_exp=5
afl_exp_count=0
for e in "${EXPS[@]}"; do
  [[ "$e" != "E3" ]] && afl_exp_count=$(( afl_exp_count + 1 ))
done
if (( afl_exp_count > 0 )); then
  total_runs=$(( afl_exp_count * afl_runs_per_exp ))
  eff_par=$(( PARALLEL < total_runs ? PARALLEL : total_runs ))
  wall_sec=$(( (total_runs * budget_sec + eff_par - 1) / eff_par ))
  wall_h=$(( wall_sec / 3600 ))
  wall_m=$(( (wall_sec % 3600) / 60 ))
  log "estimated wall-clock (AFL only, ${afl_exp_count} exps × ${afl_runs_per_exp} runs × ${budget_sec}s / ${eff_par}): ~${wall_h}h${wall_m}m"
  hr
fi

# --- Phase 1: preflight
if [[ ${SKIP_PREFLIGHT} -eq 0 ]]; then
  log "PHASE preflight: host"
  if ! bash scripts/paper01/preflight.sh --host; then
    log "ERROR: preflight failed; fix the above and re-run (or pass --skip-preflight)"
    exit 1
  fi
  log "PHASE preflight: PASS"
  hr
else
  log "PHASE preflight: SKIPPED"
  hr
fi

# --- Phase per-exp: run_batch
FAILS=0
declare -a FAILED_EXPS
for EXP in "${EXPS[@]}"; do
  log "PHASE ${EXP}: run_batch.sh --exp ${EXP} --resume --parallel ${PARALLEL}"
  args=(--exp "${EXP}" --resume --parallel "${PARALLEL}")
  [[ ${DRY_RUN} -eq 1 ]] && args+=(--dry-run)
  if bash scripts/paper01/run_batch.sh "${args[@]}"; then
    log "PHASE ${EXP}: complete"
  else
    log "ERROR: PHASE ${EXP} failed; continuing to next exp"
    FAILS=$(( FAILS + 1 ))
    FAILED_EXPS+=("${EXP}")
  fi
  hr
done

# --- Phase aggregate
if [[ ${SKIP_AGGREGATE} -eq 0 && ${DRY_RUN} -eq 0 ]]; then
  log "PHASE aggregate: aggregate.py all"
  if ! python3 scripts/paper01/aggregate.py all; then
    log "WARN: aggregate.py failed (non-fatal); see ${MASTER_LOG}"
  fi
  log "PHASE aggregate: prepare_paper01_data.py"
  if ! python3 scripts/prepare_paper01_data.py \
        --run-root results/paper01_ai_recipe_mutation/runs \
        --out-dir  results/paper01_ai_recipe_mutation \
        --manifest experiments/manifests/paper01_preprint.yaml; then
    log "WARN: prepare_paper01_data.py failed (non-fatal); see ${MASTER_LOG}"
  fi
  log "PHASE aggregate: done"
  log ""
  log "Key outputs:"
  log "  results/paper01_ai_recipe_mutation/validity_report.md"
  log "  results/paper01_ai_recipe_mutation/tables/T1_per_run_summary.md"
  log "  results/paper01_ai_recipe_mutation/tables/throughput_parity.md"
  log "  results/paper01_ai_recipe_mutation/tables/coverage_summary.csv"
  log "  results/paper01_ai_recipe_mutation/tables/overhead.csv"
  hr
else
  log "PHASE aggregate: SKIPPED"
  hr
fi

if (( FAILS > 0 )); then
  log "DONE with FAILURES — failed exps: ${FAILED_EXPS[*]}"
  exit 1
fi
log "DONE — all exps succeeded."
exit 0
