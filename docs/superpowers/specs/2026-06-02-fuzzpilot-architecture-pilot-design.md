# FuzzPilot Architecture Pilot Design

## Purpose

This design upgrades FuzzPilot from a working AFL++/LLM prototype into a paper-ready
architecture pilot. The goal is not to claim that "LLMs fuzz better" in general.
The claim is narrower and stronger:

> LLMs can improve greybox fuzzing when they are kept off the mutation hot path,
> grounded in target semantics, and empirically validated by short micro-campaigns
> before their strategies are promoted into the main campaign.

The pilot must produce credible evidence on this architecture without adding
environment noise. On the current host, that means a controlled 4-core experiment
plan, fixed resource slots, blocked run ordering, and conservative parallelism.

## Current Project Baseline

FuzzPilot already has the core architecture:

- AFL++ main campaign and telemetry collection.
- Plateau/heartbeat-triggered agent invocation.
- GLM/OpenAI-compatible model gateway with schema validation and decision logs.
- Ghidra/static-analysis context path.
- Micro-campaign planning, evaluation, and promotion.
- Recipe-guided custom mutator.
- Ablation support for `baseline-afl`, `rule-only`, `no-static-analysis`,
  `no-mutator`, `no-microcampaign`, `no-plateau`, `random-recipe`,
  `random-reward`, and `edges-only`.

The libxml2 architecture path is configured for the free `glm-4.7-flash` GLM
text model. A real stage must still prove credential validity with
`scripts/paper01/model_auth_smoke.py`; HTTP 401/403 model calls invalidate the
stage and must not be treated as paper evidence. Main AFL++ restarts after
inline micro-campaigns use fresh output directories.

## Research Basis

The design uses these papers as pressure tests, not as features to copy blindly:

- FunFuzz (2026, arXiv 2605.02789): island-style evolutionary search suggests
  keeping several diverse strategy sources alive instead of collapsing all
  proposals into one best guess.
- FuzzingBrain V2 (2026, arXiv 2605.21779) and FuzzAgent (2026, arXiv
  2605.14431): multi-agent fuzzing systems with concrete execution evidence
  make "many agents" an insufficient novelty claim. FuzzPilot should instead
  isolate the architecture of off-hot-path AFL++ campaign control on a fixed
  libxml2 harness.
- MASFuzzer (2026, arXiv 2604.17977): fuzz-driver generation needs scheduling
  and validation, not only LLM output.
- ELFuzz (USENIX Security 2025): useful LLM work can be done outside the hot
  fuzzing loop; FuzzPilot adopts this as a hard system invariant.
- SeedAIchemy (2025, arXiv 2511.12448): seed corpus quality matters enough to be
  controlled separately from the main architecture claim.
- ChatAFL (NDSS 2024) and MultiFuzz (2025): semantic context and retrieval help
  constrain LLM guidance. FuzzPilot should use target knowledge to improve
  proposals without making LLM calls during mutation.
- FOX (2024, arXiv 2406.04517): coverage-guided fuzzing can be viewed as online
  control. FuzzPilot's micro-campaign scheduler should spend validation budget
  adaptively, not uniformly.
- SoK: Prudent Evaluation Practices for Fuzzing (2024): claims must use repeats,
  controlled environments, fixed budgets, and statistical reporting.

## Paper Contribution Shape

The pilot paper should present FuzzPilot as a system architecture contribution:

1. **Three-plane architecture**:
   - Data plane: AFL++ and the custom mutator, fast and deterministic.
   - Semantic control plane: Ghidra context, telemetry, agent memory, and LLM
     agents, invoked only at window boundaries.
   - Validation plane: short AFL++ micro-campaigns that score proposals before
     promotion.

2. **Micro-campaign validation protocol**:
   LLM proposals cannot directly affect the main campaign unless they beat a
   control arm under a bounded AFL++ validation budget.

3. **Recipe strategy interface**:
   Agents and static analysis communicate with the mutator through structured
   recipes/interventions rather than byte strings. This makes decisions
   auditable, replayable, and ablatable.

4. **Multi-agent semantic planning**:
   The eight-agent layer is framed as fuzzing-decision decomposition, not as a
   raw agent-count claim. Each agent proposes from a different strategy family;
   micro-campaigns act as the common judge.

5. **Low-noise evaluation protocol for LLM-assisted fuzzing**:
   The paper should make environment control part of the contribution because
   LLM latency, API rate limits, CPU contention, and AFL++ nondeterminism can
   otherwise dominate results.

## Required System Additions

### 1. `ai-direct` ablation

Add an ablation mode where the agent proposal bypasses micro-campaign validation
and is promoted directly. This proves whether the validation plane is necessary.

Expected behavior:

- Agents still run normally.
- No validation micro-campaign is launched.
- The first valid non-control proposal is converted into a recipe and promoted.
- Events must include `ai_direct_promoted`.
- Report must mark `winner_status=selected_direct` or an equivalent explicit
  status to avoid conflating direct promotion with validated promotion.

### 2. Agent subset ablation

Add an agent selection switch or mode so experiments can run:

- `single-agent-coordinator`
- `single-agent-dictionary`
- `full-agent`

This tests whether eight-agent decomposition contributes beyond a single LLM
planner. The implementation should reuse the existing `configure_agent_tasks`
filter path rather than duplicating agent setup.

### 3. Semantic context ablation

Add a first-class `no-semantic-context` mode. It differs from
`no-static-analysis` by also withholding retrieved target knowledge and
dictionary/token hints from the blackboard. It should preserve telemetry so the
experiment isolates semantic context, not all context.

### 4. Bandit micro-campaign scheduler

Replace fixed top-N validation with a conservative two-stage scheduler:

1. Give each candidate a short equal "probe" budget.
2. Allocate the remaining validation budget to candidates with the best
   reward/uncertainty score.

The first implementation can use deterministic UCB-like scoring:

```
score = mean_reward + c * sqrt(log(total_probe_budget + 1) / (candidate_budget + 1))
```

This is enough for the architecture pilot. It does not need a full RL system.

### 5. Pilot experiment runner

Add a dedicated runner for this host class:

- Detect CPU count and memory.
- Reserve one CPU for OS/controller overhead when possible.
- Use fixed `taskset` CPU slots.
- Run full-agent and LLM-bearing modes at parallelism 1 unless explicitly
  overridden.
- Run non-LLM modes at parallelism 2 or 3 depending on target memory pressure.
- Use blocked randomization: run repeat blocks in mixed mode order so time-of-day
  and thermal/API effects do not align with a single mode.
- Emit per-run metadata: git status hash, CPU model, CPU slot, memory, AFL++
  version, model name, endpoint host, token counts, LLM latency, and cost fields
  if available.
- Never write API keys to logs or metadata.

## Pilot Experiment Matrix

The pilot matrix is intentionally smaller than the full venue matrix:

- Targets: `libxml2`.
- Modes:
  - `baseline-afl`
  - `rule-only`
  - `full-agent`
  - `ai-direct`
  - `no-static-analysis`
  - `no-semantic-context`
  - `single-agent-coordinator`
  - `single-agent-dictionary`
  - `no-mutator`
- Repeats:
  - Stage A smoke: 1 repeat, 20 minutes per cell.
  - Stage B pilot: 3 repeats, 2 hours per cell.
- Stage C paper pilot: 5 repeats, 4 hours per cell.

This is the maximum credible local plan for the current single-target libxml2
architecture track on a 4-vCPU, 8GiB, no-swap machine. Stage C is roughly
1 target x 9 modes x 5 repeats x 4 hours = 180 core-hours. With conservative
scheduling, this is still a multi-day run. It should not be launched until
Stage A and Stage B pass acceptance gates.

## Metrics

Primary metrics:

- Coverage over time, reported as median and IQR.
- Paths and bitmap edges at fixed checkpoints.
- Time from plateau to next new path.
- Throughput ratio: mode median exec/sec divided by `baseline-afl`.
- Proposal count, schema-valid rate, fallback rate.
- Validation win rate and promotion rate.
- Main-run impact after promotion: new paths/edges in 10 and 30 minute windows.

Secondary metrics:

- LLM latency p50/p95.
- Token counts and estimated cost per useful promotion.
- Micro-campaign false positive proxy: validated proposal with no subsequent
  main-run gain.
- Micro-campaign false negative proxy: rejected proposal whose shadow replay
  would have helped. This can be deferred if shadow replay is too expensive.

## Acceptance Gates

Stage A smoke must satisfy:

- Build and CTest pass.
- Every mode produces `coverage.csv`, `events.jsonl`, `report.md`, and status.
- LLM-bearing modes produce `agent_decisions.jsonl` with no auth errors.
- `full-agent` has at least one plateau/heartbeat and one agent decision.
- No run writes API keys or bearer tokens.

Stage B pilot must satisfy:

- At least 3 complete repeats per selected cell.
- `full-agent` schema-valid rate >= 0.8.
- `full-agent` throughput ratio >= 0.70 versus baseline for the same target.
- At least one target shows a validated promotion or clear no-promotion evidence.
- Runner metadata proves CPU slot and run order were recorded.

Stage C paper pilot should only start if Stage B does not show infrastructure
failures or severe throughput collapse.

Before Stage B/C starts, freeze the manifest, model name, target binary hash,
seed corpus, dictionary, static context hash, and acceptance gates. If prompt
tuning, model changes, or micro-campaign budget changes are made after looking
at partial outcomes, label the next run as a new exploratory stage and exclude
the earlier cells from the primary evidence table.
The executable freeze/check mechanism is
`scripts/paper01/architecture_freeze_inputs.py`; real Stage B/C wrappers and
readiness checks require the frozen inputs to match.

## Noise-Control Policy

For this machine:

- Default fuzzing slots: 3.
- Default LLM-bearing slots: 1.
- Avoid running multiple full-agent runs concurrently because GLM/API latency and
  rate limits can contaminate intervention timing.
- Avoid using `/tmp` for large results because root filesystem has less space.
  Store runs under `/www/fuzz_agent/results/...`.
- Keep generated `work_*` directories out of git.
- Keep CPU affinity stable per run.
- Prefer block order such as:

```
repeat 1: baseline-afl, rule-only, full-agent, ai-direct, no-static-analysis
repeat 2: rule-only, baseline-afl, ai-direct, full-agent, no-mutator
repeat 3: no-static-analysis, full-agent, baseline-afl, single-agent, rule-only
```

The exact order can be generated deterministically from a fixed seed and saved
with the manifest.

## Non-Goals for This Pilot

- Full 6-10 target venue evaluation.
- Head-to-head reproduction of all LLM fuzzers.
- CVE-class bug disclosure pipeline.
- Full shadow-run precision/recall study.
- LLM-generated harnesses.

These are Paper 3 tasks. The pilot must first establish that the architecture
works and that each layer has measurable attribution.

## Deliverables

- New ablation modes and switches for `ai-direct`, agent subsets, and
  semantic-context ablation.
- Bandit-style micro-campaign scheduler with deterministic tests.
- Low-noise pilot runner and manifest.
- Aggregation/reporting updates for proposal validation and throughput ratio.
- Documentation explaining how to reproduce Stage A and Stage B.
- A clean artifact boundary: no API keys, no raw work directories in git, and
  all generated results under ignored result paths.
