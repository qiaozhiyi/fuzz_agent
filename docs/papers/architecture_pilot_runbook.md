# FuzzPilot Architecture Pilot Runbook

This runbook executes the low-noise architecture pilot described in
`docs/superpowers/specs/2026-06-02-fuzzpilot-architecture-pilot-design.md`.
Use `docs/papers/architecture_pilot_paper_skeleton.md` for the paper RQs,
section plan, and canonical table/figure mapping.

## Host Rules

- Do not run another fuzzing batch concurrently.
- Keep results under `results/fuzzpilot_architecture_pilot/`.
- Use `/www`, not `/tmp`, for large artifacts.
- Keep `FUZZPILOT_MODEL_API_KEY` only in the shell environment.
- Rotate the key if it appears in logs, screenshots, commits, or reports.
- Do not compare runs from different CPU-count or OS environments in the same
  primary table.
- Start a stage only when pre-existing host load and memory availability pass
  preflight. The default load gate is `MAX_PREEXISTING_LOAD_PER_CPU=0.50`, so
  this 4-vCPU host refuses to start when the 1-minute load average is above 2.0.
  The default memory gate is `MIN_MEM_AVAILABLE_KIB=2097152`.

## Preflight

```bash
git status --short
scripts/paper01/with_model_key.sh \
  scripts/paper01/model_auth_smoke.py --config experiments/targets/libxml2/config_glm.yaml
scripts/paper01/with_model_key.sh \
  scripts/paper01/model_auth_smoke.py --config experiments/targets/libxml2/config_glm.yaml \
    --free-glm-candidates --show-response
scripts/paper01/with_model_key.sh \
  scripts/paper01/architecture_pilot_readiness.py S --real-run
```

The readiness audit is the default pre-run gate. It runs the architecture
preflight, shell/Python syntax checks, build, CTest, whitespace checks,
forbidden-secret scan, model-auth smoke test, resource-policy checks, and stage
resource estimates. Use `--skip-build` or `--skip-tests` only after those
commands have already passed in the same shell session.

For real runs, `scripts/paper01/model_auth_smoke.py` must return `error_kind=ok`
before launching a stage. HTTP 401/403 is an invalidating credential failure; do
not continue to Stage S/A/B/C until the shell environment contains a valid
`FUZZPILOT_MODEL_API_KEY`. Use `scripts/paper01/with_model_key.sh` when the key
is not already exported; it reads from the terminal without putting the key in
command-line arguments, shell history, stage plans, or logs.

The default libxml2 architecture config uses `glm-4.7-flash`, the current
free GLM text model selected for this pilot because it has a larger context and
output budget than the older `glm-4-flash` line and is better aligned with
agentic coding workloads. Use the `--free-glm-candidates --show-response` smoke
test before Stage S when rotating keys or confirming whether the account has
access to the free model set.

For Stage B/C, freeze the paper inputs before launching the stage:

```bash
scripts/paper01/architecture_freeze_inputs.py B --write
scripts/paper01/with_model_key.sh \
  scripts/paper01/architecture_pilot_readiness.py B --real-run
```

The freeze file records hashes for the architecture manifest, libxml2 target
config, target binary, seed corpus, dictionary, static context, model name,
endpoint host, stage budget, modes, repeats, and acceptance gates. It never
records the API-key value. Real Stage B/C wrappers check the frozen inputs by
default and refuse to start if any of these inputs drift. Use `--overwrite` only
for a deliberately new exploratory stage, not for cells that will share a
primary paper table.

If `FUZZPILOT_MODEL_API_KEY` is unset, the preflight warns and real runs mark
LLM-bearing cells as `skipped-missing-api-key`. The acceptance report treats
those skipped cells as a failed gate, because the architecture matrix is then
incomplete. The stage wrapper is stricter: for `DRY_RUN=0`, it refuses to start
unless `FUZZPILOT_MODEL_API_KEY` is set. Use `REQUIRE_API_KEY=0` only for an
intentional non-LLM infrastructure drill that will not be used as paper
evidence.

Do not relax `MAX_PREEXISTING_LOAD_PER_CPU` or `MIN_MEM_AVAILABLE_KIB` for paper
runs. If a one-off infrastructure drill needs looser checks, document the
override and keep the output out of the primary tables.

## Stage S Infrastructure Drill

Stage S is a short infrastructure drill before Stage A. It runs a stage-specific
subset of modes (`baseline-afl`, `full-agent`, `ai-direct`, and
`single-agent-coordinator`) for 5 minutes per cell. It is not paper evidence and
must not be mixed into the primary tables. Its acceptance profile is
`infrastructure`, so the report checks matrix completion, artifacts, LLM auth,
metadata, ordering, model consistency, and secret hygiene, but skips paper
evidence gates such as plateau count, validated promotion evidence, schema-valid
rate, and throughput ratio.

```bash
scripts/paper01/with_model_key.sh \
  scripts/paper01/run_architecture_stage.sh S
```

On this host, Stage S is 4 cells, 0.4 core-hours, and roughly 17 conservative
wall-clock minutes. Use it to catch broken model credentials, target crashes,
artifact copying failures, and summary/report wiring before spending a full
Stage A window.

## Stage A Smoke

Stage A runs one 20-minute cell per mode on libxml2. It is an infrastructure
check, not paper evidence. With 9 cells and serial LLM-bearing cells, expect
roughly 2.4 conservative wall-clock hours on this host.

```bash
scripts/paper01/with_model_key.sh \
  scripts/paper01/run_architecture_stage.sh A
```

The stage wrapper runs preflight, launches the low-noise runner, then writes the
CSV summary, Markdown acceptance report, and structured status JSON under
`results/fuzzpilot_architecture_pilot/`. For an unattended run:

```bash
mkdir -p /www/fuzz_agent/results/fuzzpilot_architecture_pilot
scripts/paper01/with_model_key.sh \
  nohup env \
  scripts/paper01/run_architecture_stage.sh A \
  >/www/fuzz_agent/results/fuzzpilot_architecture_pilot/stage_a.nohup.log 2>&1 &
```

Monitor:

```bash
scripts/paper01/architecture_stage_status.py A
```

The status command is read-only. It reports expected/discovered cells, status
counts, failed or skipped cells, running cells, and the latest wrapper
`*_status.json` with links to the stage logs and acceptance report. It also
prints the manifest-derived total resource estimate and a remaining estimate
that excludes `completed` cells. Use the remaining wall-clock estimate before
resuming a partially completed Stage B/C run.

For real stages, the wrapper enables `--fail-on-gate-fail` when it invokes the
summary script. `summary_exit_code=3` means the runner finished but one or more
acceptance gates failed. Dry-runs leave this disabled because their completion
and agent-decision gates are expected to fail.

For real Stage B/C runs, the wrapper also enforces stage progression:
Stage B requires a clean Stage A wrapper status and acceptance report; Stage C
requires a clean Stage B wrapper status and acceptance report. Set
`REQUIRE_PREVIOUS_STAGE_PASS=0` only for an intentional non-paper infrastructure
drill.

## Stage B Pilot

Run only after Stage A has no infrastructure failures. The wrapper refuses to
start a real Stage B if Stage A does not have a clean status JSON and acceptance
report.

```bash
scripts/paper01/architecture_freeze_inputs.py B --write
scripts/paper01/with_model_key.sh \
  scripts/paper01/run_architecture_stage.sh B
```

Stage B is the first credible architecture-pilot dataset: 3 repeats, 2 hours
per cell, blocked ordering, fixed CPU slots. On this host, the manifest estimate
is 27 cells, 54 core-hours, and roughly 46 conservative wall-clock hours because
LLM-bearing cells run serially.

## Stage C Paper Pilot

Run only if Stage B passes the acceptance gates in the design spec. The wrapper
refuses to start a real Stage C if Stage B does not have a clean status JSON and
acceptance report.

```bash
scripts/paper01/architecture_freeze_inputs.py C --write
scripts/paper01/with_model_key.sh \
  scripts/paper01/run_architecture_stage.sh C
```

Stage C is the local paper-pilot dataset: 5 repeats, 4 hours per cell. It is a
single-target libxml2 architecture pilot, so it supports architecture
validation and ablation attribution but not broad target-generalization claims.
The primary matrix has 9 modes, including both `single-agent-coordinator` and
`single-agent-dictionary`, so the multi-agent claim is not reduced to a single
planner-only comparison. On this host, the manifest estimate is 45 cells, 180
core-hours, and roughly 153 conservative wall-clock hours.

## Stage Resource Estimates

The status script computes these from the manifest:

| stage | cells | LLM-bearing | non-LLM | budget/cell | core-hours | conservative wall-clock |
|---|---:|---:|---:|---:|---:|---:|
| S | 4 | 3 | 1 | 5 min | 0.4 | 0.3 h |
| A | 9 | 7 | 2 | 20 min | 3 | 2.4 h |
| B | 27 | 21 | 6 | 2 h | 54 | 46 h |
| C | 45 | 35 | 10 | 4 h | 180 | 153 h |

The wall-clock estimate is intentionally conservative: non-LLM cells use
`defaults.non_llm_parallel=3`, while LLM-bearing cells use
`defaults.llm_parallel=1` to avoid API latency, agent execution, and fuzzing
throughput contaminating each other. Use these estimates when choosing a quiet
run window. Do not start Stage B/C unless the machine can stay dedicated for the
full window.

## Resource Policy

The runner reserves CPU 0 when the host has at least four CPUs. Non-LLM cells
use up to `defaults.non_llm_parallel` fixed fuzzing slots, while LLM-bearing
cells run serially after any active non-LLM cells drain. This uses the current
4-vCPU host without mixing API latency, agent execution, and AFL++ throughput in
the same measurement window.

Each run writes `runner_metadata.json` with git revision/dirty state, CPU model,
memory size, fuzzing CPU slots, FuzzPilot binary hash, AFL++ version, target
binary hash, and model provider/model/endpoint host. API keys are recorded only
as set/unset state.

The stage wrapper also starts `architecture_noise_monitor.py` after preflight
and stops it after the runner finishes. The monitor writes
`*_noise.jsonl` samples and `*_noise_summary.json` under the stage log prefix,
recording load average, memory availability, swap use, disk availability, and
fuzzing-process counts. These files are evidence for runtime host stability; the
monitor samples at low frequency and does not alter scheduling. The acceptance
report includes a runtime-noise gate, and the evidence bundle refuses stages
whose noise summary is missing or contains warnings by default.

Runtime warning thresholds are:

- `NOISE_MAX_LOAD1_PER_CPU=1.25`
- `NOISE_MIN_MEM_AVAILABLE_KIB=1048576`
- `NOISE_MIN_DISK_FREE_GIB=20`

The load threshold allows the expected 3 fixed fuzzing slots plus controller/API
overhead on this 4-vCPU host, while still catching obvious external CPU
contention. Do not relax these thresholds for paper evidence; use overrides only
for a documented non-paper drill.

Each wrapper invocation also writes `*_stage_plan.json` before preflight. The
stage plan records the manifest hash, expected cell IDs, target/mode/repeat
matrix, dry-run/resume policy, stage progression policy, API-key set/unset
state, CPU slot plan, parallelism, conservative resource estimate, git head, and
runtime-noise thresholds. The acceptance report verifies the stage plan against
the manifest matrix, resource policy, and paper-profile execution policy. A
paper-profile stage is not citable if it was a dry-run, did not require the API
key, or ran with gate failures disabled. Treat this as the stage-level
counterpart to each cell's `runner_metadata.json`.

Static-analysis context is precomputed and fixed per target through
`static_analysis.context_path`; the runner records the context hash in metadata.
This keeps Ghidra/static extraction out of the measured fuzzing window. The
`no-static-analysis` and `no-semantic-context` ablations disable or suppress
that same fixed context, so the comparison isolates semantic guidance rather
than Ghidra runtime overhead.

All architecture-pilot targets use the same OpenAI-compatible GLM configuration
from `experiments/targets/libxml2/config_glm.yaml` and the same
`FUZZPILOT_MODEL_API_KEY` environment variable. The current default is
`glm-4.7-flash` at `open.bigmodel.cn`. The preflight and summary report both
fail if target configs drift to different model providers, model names, endpoint
hosts, or key environment variables.

## Resume Policy

Re-running the same stage is resumable by default. A cell with `status` equal to
`completed` and `exit_code` equal to `0` is reported as `SKIP-COMPLETED` and is
not launched again. Failed, skipped, dry-run, and stale `running` cells are
eligible to run again, so an interrupted stage can be continued without deleting
successful cells.

Before resuming an interrupted stage, inspect:

```bash
scripts/paper01/architecture_stage_status.py A
```

Then re-run the same wrapper command. The wrapper keeps old completed cells,
continues incomplete cells, and writes a fresh status JSON and summary report.

To intentionally repeat already completed cells, set:

```bash
scripts/paper01/with_model_key.sh \
  env RERUN_COMPLETED=1 scripts/paper01/runners/run_architecture_pilot.sh A
```

Use forced reruns only for infrastructure mistakes that invalidate the whole
stage, and document the reason in the paper notes. Do not mix old successful
cells and forced reruns from a changed binary, changed target, changed static
context, or changed model configuration in the same primary table.

Each cell records `run_order_index` and `blocked_order_index` in
`runner_metadata.json`. The summary report fails if the stage does not have a
unique contiguous run order, which makes interrupted/resumed batches auditable.

## Result Interpretation

Use `docs/papers/architecture_pilot_claim_matrix.md` as the claim/evidence map
when turning stage artifacts into paper text. It defines which generated report
sections support each architecture claim, which ablation owns the comparison,
and when a result should narrow the claim instead of being tuned away.

Use the acceptance gates as infrastructure checks first. A stage is not paper
evidence if the matrix is incomplete, model configuration drifts, LLM auth
errors appear, or runner metadata is missing.

After the gates pass, use `Architecture Claim Effect Sizes` as the primary
paper table. It reports paired target/repeat median ratios, deterministic
bootstrap 95% confidence intervals, and paired win rates for the architecture
claims:

- `full-agent / baseline-afl` for end-to-end gain.
- `full-agent / no-semantic-context` for semantic grounding.
- `full-agent / no-static-analysis` for static-analysis grounding.
- `full-agent / ai-direct` for micro-campaign validation.
- `full-agent / single-agent-coordinator` and
  `full-agent / single-agent-dictionary` for multi-agent decomposition.
- `full-agent / no-mutator` for the recipe mutator interface.

Use `LLM Accounting` to report token cost, latency, and fallback rate. Use
`Promotion Impact Windows` to report whether validated or direct promotions led
to new paths/edges in the following 10 and 30 minutes. These tables are
secondary evidence: they explain why the architecture works or fails, while the
paired effect-size table carries the main claim.

## Paper Evidence Bundle

After a real Stage B or Stage C passes the gates, package the minimal paper
evidence bundle:

```bash
scripts/paper01/architecture_pilot_bundle.py B
```

The bundle tool refuses missing cells, non-clean wrapper status, failed
acceptance gates, missing `runner_metadata.json`, missing runtime noise summary,
missing stage plan, and runtime noise warnings by default. It packages the
manifest, stage plan, acceptance report, summary CSV, wrapper status/logs,
runtime noise summary, and per-cell metadata/report files, but intentionally
does not copy AFL++ queue/corpus directories.
