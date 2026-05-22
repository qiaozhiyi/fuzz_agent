# Evaluation Plan

## Goal

Measure whether FuzzPilot improves AFL++ campaign outcomes after low-progress
windows under equal budgets.

## Current Matrix

| Mode | Purpose |
|---|---|
| `baseline-afl` | AFL++ without FuzzPilot control |
| `rule-only` | deterministic controller logic without model calls |
| `no-static-analysis` | full loop without Ghidra context |
| `no-mutator` | full loop without recipe-guided mutation |
| `full-agent` | configured FuzzPilot loop |

Do not claim modes that are not implemented or not run.

## Initial Targets

| Target | Role |
|---|---|
| cJSON | small structured text parser |
| libpng | binary parser with chunk structure |
| vuln_target | controlled synthetic check |

The paper-level target set should expand beyond these.

## Metrics

Fuzzing outcomes:

- paths or edges over time
- unique crashes after replay/deduplication
- time to first new path after plateau
- time to first unique crash
- exec/sec

Controller outcomes:

- plateau events detected
- interventions proposed
- interventions promoted
- micro-campaign reward
- recipe hit/miss counters
- model decision `schema_valid` rate
- fallback rate
- model latency and cost

## Fairness Rules

- Use the same total budget for comparable modes.
- Use the same seed corpus for a target.
- Rebuild target binaries on the experiment machine.
- Use fresh output directories for every repeat.
- Record AFL++ version, OS, CPU, commit, config hash, and target binary hash.
- Run multiple repeats per target and mode.
- Treat Docker as the experiment boundary. macOS/Linux and amd64/arm64 hosts may
  run the workflow, but paper-comparable data should use
  `FUZZPILOT_DOCKER_PLATFORM=linux/amd64`.

## Crash Handling

Crash results need replay and deduplication before being counted. Record:

- crashing input path
- replay command
- sanitizer settings
- stack or deduplication key
- whether the result is crash, hang, OOM, or flaky

## Run Bundle

Each run should preserve:

```text
results/<run_id>/
  run_metadata.json
  git_status.txt
  git.patch
  report.md
  coverage.csv
  events.jsonl
  agent_decisions.jsonl
  agent_memory.jsonl
  fuzzpilot.sqlite
  main_launch.sh
  promoted_recipes/
```

Metadata helper:

```bash
scripts/capture_run_metadata.sh \
  --run-id run_cjson_001 \
  --config experiments/targets/cjson/config.yaml \
  --target cjson_parser \
  --out-dir results/run_cjson_001
```

## Next Work

1. Reproduce `docs/quickstart.md` on a clean Docker-capable machine.
2. Run the Docker smoke on the host-native platform and on `linux/amd64`.
3. Run short real-run smokes for every matrix mode.
4. Add crash replay/deduplication scripts.
5. Run repeated cJSON and libpng experiments.
6. Add more targets after the first matrix is stable.
7. Choose the first paper angle based on measured results.
