#!/usr/bin/env bash
# scripts/paper01/auto_run.sh
#
# Hands-off wrapper for Paper 1 experiments. Composes the existing pieces:
#   1. scripts/paper01/preflight.sh         — verify environment
#   2. scripts/paper01/run_batch.sh         — launch the batch (with --resume)
#   3. scripts/paper01/aggregate.py         — post-run aggregation (T1, F5, etc.)
#   4. scripts/prepare_paper01_data.py      — overhead.csv / coverage_summary.csv /
#                                             validity_report.md
#
# The wrapper itself adds: wall-clock estimate, master log file, optional
# background launch (nohup), and skipping of any of the four phases.
#
# Usage:
#   scripts/paper01/auto_run.sh --exp E1a [--parallel N] [--background]
#                               [--budget-sec N] [--skip-preflight]
#                               [--no-aggregate] [--dry-run]
#
# Examples:
#   # 4-way parallel, foreground, then auto-aggregate
#   scripts/paper01/auto_run.sh --exp E1a --parallel 4
#
#   # Background launch — returns immediately, watch via tail -f
#   scripts/paper01/auto_run.sh --exp E1a --parallel 4 --background
#
#   # Plumbing check without actually fuzzing
#   scripts/paper01/auto_run.sh --exp E1a --dry-run
#
# Exit codes:
#   0  batch + aggregation succeeded (or backgrounded successfully)
#   1  batch failed
#   2  preflight or argument error

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RESULTS_ROOT="${REPO_ROOT}/results/paper01_ai_recipe_mutation"
LOG_DIR="${RESULTS_ROOT}/runs/_logs"
MANIFEST="${REPO_ROOT}/experiments/manifests/paper01_preprint.yaml"

EXP=""
PARALLEL=4
BUDGET_OVERRIDE=""
BACKGROUND=0
SKIP_PREFLIGHT=0
NO_AGGREGATE=0
DRY_RUN=0

usage() {
  sed -n '2,32p' "${BASH_SOURCE[0]}"
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exp)             EXP="$2"; shift 2 ;;
    --parallel)        PARALLEL="$2"; shift 2 ;;
    --budget-sec)      BUDGET_OVERRIDE="$2"; shift 2 ;;
    --background)      BACKGROUND=1; shift ;;
    --skip-preflight)  SKIP_PREFLIGHT=1; shift ;;
    --no-aggregate)    NO_AGGREGATE=1; shift ;;
    --dry-run)         DRY_RUN=1; shift ;;
    -h|--help)         usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -z "${EXP}" ]] && { echo "ERROR: --exp is required" >&2; usage; }
[[ -f "${MANIFEST}" ]] || { echo "ERROR: manifest missing: ${MANIFEST}" >&2; exit 2; }

mkdir -p "${LOG_DIR}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
MASTER_LOG="${LOG_DIR}/${EXP}_auto_run_${TIMESTAMP}.log"

log() { echo "[$(date -u +%H:%M:%SZ)] $*" | tee -a "${MASTER_LOG}"; }
fail() { log "FATAL: $*"; exit "${2:-1}"; }

# --- Resolve manifest details for this experiment (queue size, budget, kind)
read -r EXP_KIND EXP_BUDGET EXP_QUEUE_LEN < <(python3 - "${MANIFEST}" "${EXP}" <<'PY'
import sys, yaml
m = yaml.safe_load(open(sys.argv[1]))
exp_id = sys.argv[2]
for e in m["experiments"]:
    if e["id"] == exp_id:
        kind = e.get("kind", "afl")
        budget = int(e.get("budget_sec", m.get("defaults", {}).get("budget_sec", 0)))
        n = len(e.get("runs", []))
        print(f"{kind} {budget} {n}")
        break
else:
    print("MISSING 0 0")
PY
)

if [[ "${EXP_KIND}" == "MISSING" ]]; then
  fail "experiment ${EXP} not found in ${MANIFEST}" 2
fi

if [[ -n "${BUDGET_OVERRIDE}" ]]; then
  EXP_BUDGET="${BUDGET_OVERRIDE}"
fi

log "=== auto_run.sh ${EXP} ==="
log "repo: ${REPO_ROOT}"
log "log:  ${MASTER_LOG}"
log "kind: ${EXP_KIND}  runs: ${EXP_QUEUE_LEN}  budget_sec: ${EXP_BUDGET}  parallel: ${PARALLEL}"

if [[ "${EXP_KIND}" != "microbench" && "${EXP_QUEUE_LEN}" -gt 0 && "${PARALLEL}" -gt 0 ]]; then
  # Effective parallel can't exceed queue length
  eff_par=$(( PARALLEL < EXP_QUEUE_LEN ? PARALLEL : EXP_QUEUE_LEN ))
  total_sec=$(( EXP_BUDGET * EXP_QUEUE_LEN ))
  wall_sec=$(( (total_sec + eff_par - 1) / eff_par ))   # ceil
  printf -v wall_h "%02d" $(( wall_sec / 3600 ))
  printf -v wall_m "%02d" $(( wall_sec % 3600 / 60 ))
  log "wall-clock estimate: ~${wall_h}h${wall_m}m  (${EXP_QUEUE_LEN} × ${EXP_BUDGET}s / ${eff_par} cores, naive)"
fi

# --- 1. Preflight
if [[ ${SKIP_PREFLIGHT} -eq 0 ]]; then
  log "[phase 1/4] preflight"
  if ! bash "${REPO_ROOT}/scripts/paper01/preflight.sh" --host >> "${MASTER_LOG}" 2>&1; then
    log "preflight FAILED — tail of log:"
    tail -20 "${MASTER_LOG}" >&2
    fail "preflight reported failures; re-run with --skip-preflight to override" 2
  fi
  log "preflight PASS"
else
  log "[phase 1/4] preflight SKIPPED"
fi

# --- 2. Build the batch command
BATCH_CMD=("${REPO_ROOT}/scripts/paper01/run_batch.sh" --exp "${EXP}" --resume --parallel "${PARALLEL}")
if [[ -n "${BUDGET_OVERRIDE}" ]]; then
  BATCH_CMD+=(--budget-sec "${BUDGET_OVERRIDE}")
fi
if [[ ${DRY_RUN} -eq 1 ]]; then
  BATCH_CMD+=(--dry-run)
fi

log "[phase 2/4] batch command: ${BATCH_CMD[*]}"

# --- 3. Launch
if [[ ${BACKGROUND} -eq 1 ]]; then
  PID_FILE="${LOG_DIR}/${EXP}_pid.txt"
  STATUS_FILE="${LOG_DIR}/${EXP}_auto_run_${TIMESTAMP}.status"
  echo "running" > "${STATUS_FILE}"
  log "backgrounding batch; nohup, detaching stdin/stdout"
  (
    nohup "${BATCH_CMD[@]}" >> "${MASTER_LOG}" 2>&1
    batch_rc=$?
    if [[ ${batch_rc} -eq 0 && ${NO_AGGREGATE} -eq 0 && ${DRY_RUN} -eq 0 ]]; then
      {
        echo "[$(date -u +%H:%M:%SZ)] [phase 3/4] aggregate.py all"
        python3 "${REPO_ROOT}/scripts/paper01/aggregate.py" all
        echo "[$(date -u +%H:%M:%SZ)] [phase 4/4] prepare_paper01_data.py"
        python3 "${REPO_ROOT}/scripts/prepare_paper01_data.py" \
          --run-root "${RESULTS_ROOT}/runs" \
          --out-dir "${RESULTS_ROOT}" \
          --manifest "${MANIFEST}"
      } >> "${MASTER_LOG}" 2>&1
    fi
    if [[ ${batch_rc} -eq 0 ]]; then
      echo "completed" > "${STATUS_FILE}"
    else
      echo "failed_rc${batch_rc}" > "${STATUS_FILE}"
    fi
    echo "[$(date -u +%H:%M:%SZ)] auto_run done rc=${batch_rc}" >> "${MASTER_LOG}"
  ) &
  BG_PID=$!
  disown "${BG_PID}" 2>/dev/null || true
  echo "${BG_PID}" > "${PID_FILE}"
  log "backgrounded pid=${BG_PID}"
  log "watch with:    tail -f ${MASTER_LOG}"
  log "status file:   ${STATUS_FILE}  (running | completed | failed_rcN)"
  log "stop with:     kill ${BG_PID}  (will not clean child run_batch.sh PIDs — use 'kill -- -${BG_PID}' if needed)"
  exit 0
fi

# Foreground
log "[phase 2/4] running batch (foreground)"
set +e
"${BATCH_CMD[@]}" 2>&1 | tee -a "${MASTER_LOG}"
BATCH_RC=${PIPESTATUS[0]}
set -e

if [[ ${BATCH_RC} -ne 0 ]]; then
  log "batch FAILED rc=${BATCH_RC}; skipping aggregation"
  exit 1
fi

if [[ ${NO_AGGREGATE} -eq 1 || ${DRY_RUN} -eq 1 ]]; then
  log "[phase 3-4/4] aggregation skipped (--no-aggregate or --dry-run)"
  exit 0
fi

log "[phase 3/4] aggregate.py all"
if ! python3 "${REPO_ROOT}/scripts/paper01/aggregate.py" all 2>&1 | tee -a "${MASTER_LOG}"; then
  log "aggregate.py failed (non-fatal); see ${MASTER_LOG}"
fi

log "[phase 4/4] prepare_paper01_data.py"
if ! python3 "${REPO_ROOT}/scripts/prepare_paper01_data.py" \
      --run-root "${RESULTS_ROOT}/runs" \
      --out-dir "${RESULTS_ROOT}" \
      --manifest "${MANIFEST}" 2>&1 | tee -a "${MASTER_LOG}"; then
  log "prepare_paper01_data.py failed (non-fatal); see ${MASTER_LOG}"
fi

log "done. validity report: ${RESULTS_ROOT}/validity_report.md"
exit 0
