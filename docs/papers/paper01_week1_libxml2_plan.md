# Paper 01 Week-1 libxml2 Closed-Loop Plan

This plan is intentionally separate from the active `venue Stage-1` remote run.

## Goals

- Reuse the already-running `libxml2 baseline-afl` evidence.
- Avoid touching the current remote `run_venue_stage1.sh` session.
- Complete a one-week closed-loop matrix on a single non-saturated target.

## Week-1 experiment set

- `W1a`: `baseline-afl`, `N=5`, `24h`
- `W1b`: `full-agent`, `N=5`, `24h`
- `W1c`: `controller-only`, `N=3`, `24h`
- `W1d`: `rule-only`, `N=3`, `24h`
- `W1e`: `no-static-analysis`, `N=3`, `24h`

## Switching rule

Do not interrupt the currently running remote `libxml2 baseline-afl` batch.

After it completes:

1. Stop the active venue-stage runner before it advances to the next target.
2. Build the week-1 binary in an isolated directory, for example `build-week1/fuzzpilot`.
3. Start `scripts/paper01/runners/run_week1_libxml2.sh --from W1b --bin /root/fuzz_agent/build-week1/fuzzpilot`.
3. Keep raw artifacts under `results/paper01_ai_recipe_mutation/runs/`.

## Dry-run

```bash
scripts/paper01/runners/run_week1_libxml2.sh --dry-run
```

```bash
scripts/paper01/runners/run_week1_libxml2.sh --dry-run --from W1c --bin /root/fuzz_agent/build-week1/fuzzpilot
```

## Notes

- This path is for week-1 paper hardening only.
- It does not replace the full venue matrix.
- It avoids editing the active remote stage1 runner so in-flight results remain valid.
