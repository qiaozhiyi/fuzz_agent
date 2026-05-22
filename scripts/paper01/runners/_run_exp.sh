#!/usr/bin/env bash
# scripts/paper01/runners/_run_exp.sh
#
# Common runner invoked by per-experiment wrappers (run_eX.sh).
# Resolves env (loads /etc/profile.d/fuzzpilot_env.sh idempotently), gates
# full-agent experiments on FUZZPILOT_MODEL_API_KEY, picks a default parallel
# matching the host's 4 cores, writes a dated log, and execs run_batch.sh.
#
# Usage: _run_exp.sh <EXP> [extra args passed through to run_batch.sh]

set -uo pipefail

[[ $# -ge 1 ]] || { echo "usage: _run_exp.sh <EXP> [args...]" >&2; exit 2; }
EXP="$1"; shift

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

# Idempotent env load: profile.d sources fuzzpilot_secrets which exports the API key.
# Safe to re-source — it just re-exports the same values.
[ -r /etc/profile.d/fuzzpilot_env.sh ] && . /etc/profile.d/fuzzpilot_env.sh

case "${EXP}" in
  E1b|E2b|E2c) NEEDS_API_KEY=1 ;;
  *)           NEEDS_API_KEY=0 ;;
esac

if (( NEEDS_API_KEY )) && [[ -z "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
  echo "ERROR: ${EXP} requires FUZZPILOT_MODEL_API_KEY but it is empty" >&2
  echo "       export FUZZPILOT_MODEL_API_KEY=<key> or check /etc/profile.d/fuzzpilot_env.sh" >&2
  exit 2
fi

# Default parallel = 4 (host has 4 cores). Override with --parallel.
PARALLEL=4
PASS_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --parallel) PARALLEL="$2"; shift 2 ;;
    *) PASS_ARGS+=("$1"); shift ;;
  esac
done

# Pre-flight (best-effort, non-blocking)
echo "===== ${EXP} runner ====="
echo "exp:      ${EXP}"
echo "parallel: ${PARALLEL}"
echo "host:     $(hostname) $(uname -m)"
echo "build:    $([ -x ${REPO_ROOT}/build/fuzzpilot ] && echo build/fuzzpilot || echo MISSING)"
if (( NEEDS_API_KEY )); then
  echo "api_key:  set (len=${#FUZZPILOT_MODEL_API_KEY})"
else
  echo "api_key:  not required"
fi

LOG_DIR="${REPO_ROOT}/results/paper01_ai_recipe_mutation/runs/_logs"
mkdir -p "${LOG_DIR}"
LOG="${LOG_DIR}/${EXP}_$(date -u +%Y%m%dT%H%M%SZ).log"
echo "log:      ${LOG}"
echo "======================="

set +e
bash "${REPO_ROOT}/scripts/paper01/run_batch.sh" \
  --exp "${EXP}" \
  --resume \
  --parallel "${PARALLEL}" \
  "${PASS_ARGS[@]}" 2>&1 | tee "${LOG}"
rc=${PIPESTATUS[0]}
set -e
echo "[${EXP}] run_batch exit rc=${rc}; log: ${LOG}"
exit ${rc}
