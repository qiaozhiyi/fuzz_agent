#!/usr/bin/env bash
# Safe week-1 handoff watcher for Paper-01 libxml2.
#
# Behavior:
#   1. Wait for the active venue-stage libxml2 baseline-afl batch to fully end.
#   2. Stop the venue-stage runner during the post-batch sleep window.
#   3. Build an isolated binary in build-week1/.
#   4. Run a short controller-only smoke test.
#   5. Start the week-1 libxml2 runner from W1b in a separate tmux session.
#
# This script never edits run_venue_stage1.sh and refuses to act once
# libxml2/full-agent has already started.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RUNS_ROOT="${REPO}/results/paper01_ai_recipe_mutation/runs"
LOG_DIR="${RUNS_ROOT}/_logs"
VENUE_STATUS="${LOG_DIR}/venue_stage1.status"
HANDOFF_STATUS="${LOG_DIR}/week1_handoff.status"
WEEK1_LOG="${LOG_DIR}/week1_libxml2.log"
BUILD_DIR="${REPO}/build-week1"
FUZZPILOT_BIN="${BUILD_DIR}/fuzzpilot"
WEEK1_RUNNER="${REPO}/scripts/paper01/runners/run_week1_libxml2.sh"
WEEK1_SESSION="${WEEK1_SESSION:-week1_libxml2}"
POLL_SEC="${POLL_SEC:-15}"
SMOKE_BUDGET_SEC="${SMOKE_BUDGET_SEC:-60}"

mkdir -p "${LOG_DIR}"

status_set() {
  printf '[%s] %s\n' "$(date -u +%FT%TZ)" "$*" | tee -a "${HANDOFF_STATUS}"
}

count_proc() {
  local pattern="$1"
  ps -eo cmd | grep "${pattern}" | grep -v grep | wc -l | tr -d ' '
}

current_stage1_log_has() {
  local pattern="$1"
  local start_line
  start_line="$(
    awk '/venue Stage-1 pilot orchestrator START/ { line = NR } END { if (line) print line; else print 1 }' \
      "${VENUE_STATUS}" 2>/dev/null
  )"
  tail -n +"${start_line}" "${VENUE_STATUS}" 2>/dev/null | grep -q "${pattern}"
}

have_full_agent_started() {
  if current_stage1_log_has 'BATCH START: target=libxml2 mode=full-agent'; then
    return 0
  fi
  [[ "$(count_proc 'p1_v_libxml2_full-agent')" -gt 0 ]]
}

have_baseline_batch_completed() {
  current_stage1_log_has 'BATCH END:   target=libxml2 mode=baseline-afl'
}

stop_venue_runner() {
  if tmux has-session -t venue_stage1 2>/dev/null; then
    tmux kill-session -t venue_stage1 || true
    status_set "killed tmux session venue_stage1"
  fi

  local pid_file
  pid_file="$(mktemp)"
  ps -eo pid,cmd | awk '/run_venue_stage1\.sh/ && !/awk/ {print $1}' > "${pid_file}"
  while IFS= read -r pid; do
    [[ -n "${pid}" ]] || continue
    kill "${pid}" 2>/dev/null || true
    status_set "sent SIGTERM to venue runner pid=${pid}"
  done < "${pid_file}"
  rm -f "${pid_file}"
}

run_smoke() {
  local smoke_root="${RUNS_ROOT}/_smoke_controller_only_$(date -u +%Y%m%dT%H%M%SZ)"
  mkdir -p "${smoke_root}"
  status_set "starting controller-only smoke at ${smoke_root}"
  if "${FUZZPILOT_BIN}" run --real-run \
      --config "${REPO}/experiments/targets/libxml2/config.yaml" \
      --ablation controller-only \
      --main-budget-sec "${SMOKE_BUDGET_SEC}" \
      --work-dir "${smoke_root}/work" \
      > "${smoke_root}/stdout.log" \
      2> "${smoke_root}/stderr.log"; then
    status_set "controller-only smoke passed"
    return 0
  fi
  status_set "controller-only smoke failed"
  return 1
}

launch_week1() {
  if tmux has-session -t "${WEEK1_SESSION}" 2>/dev/null; then
    status_set "tmux session ${WEEK1_SESSION} already exists; aborting"
    return 1
  fi
  tmux new-session -d -s "${WEEK1_SESSION}" \
    "cd '${REPO}' && bash '${WEEK1_RUNNER}' --from W1b --bin '${FUZZPILOT_BIN}' 2>&1 | tee '${WEEK1_LOG}'"
  status_set "launched week1 session=${WEEK1_SESSION} log=${WEEK1_LOG}"
}

status_set "handoff watcher start repo=${REPO} poll=${POLL_SEC}s"

while true; do
  if have_full_agent_started; then
    status_set "missed safe window: libxml2 full-agent already started; aborting"
    exit 1
  fi

  if have_baseline_batch_completed; then
    baseline_count="$(count_proc 'p1_v_libxml2_baseline-afl')"
    if [[ "${baseline_count}" == "0" ]]; then
      status_set "baseline batch completed and no active baseline processes remain"
      stop_venue_runner
      sleep 2
      if have_full_agent_started; then
        status_set "full-agent started during handoff; aborting to avoid touching new runs"
        exit 1
      fi
      break
    fi
    status_set "baseline batch end marker seen but ${baseline_count} baseline processes still active"
  fi

  sleep "${POLL_SEC}"
done

status_set "building isolated week1 binary at ${BUILD_DIR}"
cmake -S "${REPO}" -B "${BUILD_DIR}" >> "${HANDOFF_STATUS}" 2>&1
cmake --build "${BUILD_DIR}" --target fuzzpilot -j4 >> "${HANDOFF_STATUS}" 2>&1
status_set "isolated week1 binary ready at ${FUZZPILOT_BIN}"

run_smoke
launch_week1
status_set "handoff complete"
