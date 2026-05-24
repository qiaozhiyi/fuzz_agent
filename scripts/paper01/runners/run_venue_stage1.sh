#!/usr/bin/env bash
# scripts/paper01/runners/run_venue_stage1.sh
#
# Paper-1 venue Stage-1 pilot orchestrator.
#
# Design (noise-minimization):
#   1. Targets run SEQUENTIALLY (libxml2 → sqlite3 → openssl_x509).
#      Eliminates cross-target working-set thrashing.
#   2. Within a target, modes run SEQUENTIALLY (each mode's N runs done
#      before next mode starts). Same-mode reps share host state.
#   3. Within a mode batch, 4-way parallel via xargs -P 4.
#      Each parallel slot is pinned to a dedicated CPU via taskset
#      (eliminates cross-core migration / dispatcher noise).
#   4. Between batches, 60s sleep for AFL forkserver / pagecache cleanup.
#   5. Status written to RUNS_ROOT/_logs/venue_stage1.status (updated
#      every batch transition) for monitoring.
#
# Per-target schedule:
#   baseline-afl   N=5  → 4 + 1  = 2 batches × 24h = 48h
#   full-agent     N=5  → 4 + 1  = 2 batches × 24h = 48h
#   rule-only      N=3  → 1 batch  × 24h          = 24h
#   no-mutator     N=3  → 1 batch  × 24h          = 24h
#   no-static-analysis N=3 → 1 batch × 24h        = 24h
# Per-target wall: 168h ≈ 7 days
# 3 targets sequential: ~21 days
#
# Usage: run_venue_stage1.sh [--dry-run] [--target <name>] [--from <mode>]
#
# Configurable: set BUDGET_SEC env var to override (default 86400).

set -o pipefail
# Note: not using `set -u` because bash assoc-array ${A[$k]} access with
# missing key trips strict mode even when guarded; we manage required-vars
# explicitly.

REPO=/root/fuzz_agent
RUNS_ROOT="$REPO/results/paper01_ai_recipe_mutation/runs"
LOGS="$RUNS_ROOT/_logs"
STATUS="$LOGS/venue_stage1.status"
BUDGET_SEC=${BUDGET_SEC:-86400}    # 24h
FUZZPILOT_BIN="$REPO/build/fuzzpilot"
NCPUS=4

mkdir -p "$LOGS"

DRY=0
ONLY_TARGET=""
FROM_MODE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY=1; shift ;;
    --target)  ONLY_TARGET="$2"; shift 2 ;;
    --from)    FROM_MODE="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

status_set() {
  printf '[%s] %s\n' "$(date -u +%FT%TZ)" "$*" | tee -a "$STATUS"
}

# Per-target schedule
declare -A TARGET_CFG=(
  [libxml2]="experiments/targets/libxml2/config.yaml"
  [sqlite3]="experiments/targets/sqlite3/config.yaml"
  [openssl_x509]="experiments/targets/openssl_x509/config.yaml"
)
TARGET_ORDER=(libxml2 sqlite3 openssl_x509)

# Mode → (count, exp_phase)
declare -A MODE_N=(
  [baseline-afl]=5
  [full-agent]=5
  [rule-only]=3
  [no-mutator]=3
  [no-static-analysis]=3
)
MODE_ORDER=(baseline-afl full-agent rule-only no-mutator no-static-analysis)

# launch_one: run one fuzzpilot invocation, pinned to a fixed CPU.
# Args: <target> <mode> <rep> <cpu> <cfg>
launch_one() {
  local target="$1" mode="$2" rep="$3" cpu="$4" cfg="$5"
  local run_id="p1_v_${target}_${mode}_r$(printf '%02d' "$rep")"
  local out_dir="$RUNS_ROOT/$run_id"
  mkdir -p "$out_dir"
  local log="$out_dir/runner.log"
  echo "[$(date -u +%FT%TZ)] launching $run_id on CPU $cpu" >> "$STATUS"
  if [[ "${DRY:-0}" == "1" ]]; then
    echo "DRY: taskset -c $cpu $FUZZPILOT_BIN run --real-run --config $cfg --ablation $mode --main-budget-sec $BUDGET_SEC --work-dir $out_dir/work"
    return 0
  fi
  cd "$REPO"
  AFL_NO_AFFINITY=1 taskset -c "$cpu" "$FUZZPILOT_BIN" run --real-run \
    --config "$cfg" \
    --ablation "$mode" \
    --main-budget-sec "$BUDGET_SEC" \
    --work-dir "$out_dir/work" \
    >> "$log" 2>&1
  local rc=$?
  echo "[$(date -u +%FT%TZ)] $run_id exit=$rc" >> "$STATUS"
  # Promote artifacts to paper layout (matches E1/E2 directory structure)
  if [ -d "$out_dir/work" ]; then
    local rd
    rd=$(find "$out_dir/work" -maxdepth 1 -type d -name 'run_*' | head -1)
    if [ -n "$rd" ]; then
      for f in fuzzer_stats coverage.csv events.jsonl agent_decisions.jsonl \
               agent_memory.jsonl main_launch.sh run_metadata.json report.md; do
        [ -f "$rd/$f" ] && cp -n "$rd/$f" "$out_dir/$f" 2>/dev/null
      done
      [ -f "$rd/main_out/default/fuzzer_stats" ] && \
        cp -n "$rd/main_out/default/fuzzer_stats" "$out_dir/fuzzer_stats" 2>/dev/null
    fi
  fi
  # Disk hygiene: 3 venue targets × 19 runs × 24h would produce
  #   * git.patch  ~920 MB/run (working-tree diff at launch; not needed
  #     once the SHA in run_metadata.json + main_launch.sh are kept)
  #   * work/      ~50 MB/run  (AFL queue / crashes / hangs; we keep
  #     fuzzer_stats / events.jsonl / coverage.csv copies above)
  #   * fuzzpilot.sqlite + WAL/SHM: ~30 MB/run (raw event store, the
  #     same info is in events.jsonl which we keep)
  # Removing these saves ~1 GB / run = ~60 GB across the full pilot,
  # which is the difference between fitting on /www and overflowing it.
  rm -rf "$out_dir/work" \
         "$out_dir/git.patch" \
         "$out_dir/fuzzpilot.sqlite"* 2>/dev/null
  if [ "$rc" = "0" ]; then echo completed > "$out_dir/status"
  else echo "failed(rc=$rc)" > "$out_dir/status"
  fi
}
export -f launch_one
export RUNS_ROOT REPO FUZZPILOT_BIN BUDGET_SEC STATUS DRY

# run_mode_batch: launch N reps of one (target,mode), 4-way parallel,
# CPU-pinned. Blocks until all N finished.
run_mode_batch() {
  local target="$1" mode="$2" n="$3" cfg="$4"
  status_set "BATCH START: target=$target mode=$mode N=$n cfg=$cfg budget=${BUDGET_SEC}s"
  local args=()
  for r in $(seq 1 "$n"); do
    cpu=$(( (r - 1) % NCPUS ))
    args+=("$target" "$mode" "$r" "$cpu" "$cfg")
  done
  # 5 args per launch_one call
  printf '%s\n' "${args[@]}" | \
    xargs -n 5 -P "$NCPUS" bash -c 'launch_one "$@"' _
  status_set "BATCH END:   target=$target mode=$mode"
  if [[ "${DRY:-0}" != "1" ]]; then
    status_set "sleeping 60s for AFL/pagecache cleanup"
    sleep 60
  fi
}

status_set "venue Stage-1 pilot orchestrator START (budget=${BUDGET_SEC}s/run, NCPUS=$NCPUS, DRY=$DRY)"

for target in "${TARGET_ORDER[@]}"; do
  [[ -n "$ONLY_TARGET" && "$target" != "$ONLY_TARGET" ]] && continue
  status_set "==== TARGET START: $target ===="
  skip_until_mode=1
  if [[ -z "$FROM_MODE" ]]; then skip_until_mode=0; fi
  for mode in "${MODE_ORDER[@]}"; do
    if (( skip_until_mode )) && [[ "$mode" != "$FROM_MODE" ]]; then
      status_set "skipping $target/$mode (--from filter)"
      continue
    fi
    skip_until_mode=0
    n="${MODE_N[$mode]}"
    cfg="${TARGET_CFG[$target]}"
    run_mode_batch "$target" "$mode" "$n" "$cfg"
  done
  status_set "==== TARGET END:   $target ===="
done

status_set "venue Stage-1 pilot orchestrator FINISHED"
