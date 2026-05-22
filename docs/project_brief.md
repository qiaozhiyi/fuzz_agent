# FuzzPilot Project Brief

## Scope

FuzzPilot is a C/C++ controller around AFL++. It monitors a running fuzzing
campaign, detects low-progress windows, proposes bounded interventions, tests
those interventions in short micro-campaigns, and records the decision trail.

The model side is not in the AFL++ mutation hot path. Model calls happen during
planning and analysis. Mutation execution remains native code through an AFL++
custom mutator and recipe store.

## Status

Current stage:

```text
prototype implementation: present
local smoke validation: present
benchmark-scale validation: missing
```

Implemented pieces:

- C++ CLI and controller
- AFL++ command construction and process execution
- AFL++ `fuzzer_stats` parsing
- telemetry logging to SQLite and JSONL
- plateau detection
- OpenAI-compatible model gateway
- default agent runtime and decision logging
- micro-campaign planning/evaluation path
- compact mutation recipe store/cache
- AFL++ custom mutator with recipe-driven operators
- cJSON, libpng, and synthetic target configs
- M6 ablation matrix generation
- Docker-anywhere smoke support for linux/amd64 and linux/arm64

## Non-Goals

- FuzzPilot is not a harness generator.
- It does not replace AFL++.
- It does not call a model for every mutation.
- Current repository results should not be presented as benchmark evidence.

## Runtime Loop

```text
AFL++ main run
  -> telemetry collection
  -> plateau check
  -> compact blackboard
  -> agent proposals
  -> schema/fallback checks
  -> micro-campaigns
  -> reward comparison
  -> promotion or fallback
```

## Code Map

| Area | Paths |
|---|---|
| CLI | `src/cli/main.cpp` |
| Controller | `src/controller/run.cpp` |
| Config | `src/config.cpp`, `include/fuzzpilot/config.hpp` |
| AFL++ runner | `src/runner/afl_runner.cpp`, `src/runner/process_posix.cpp` |
| Telemetry | `src/telemetry/` |
| Plateau detection | `src/plateau/detector.cpp` |
| Agent runtime | `src/agents/agent_runtime.cpp` |
| Model gateway | `src/model/gateway.cpp` |
| SQLite schema | `db/schema.sql` |
| Micro-campaigns | `src/micro/` |
| Recipe store | `src/mutation/` |
| AFL++ mutator | `mutators/fuzzpilot/` |
| Experiment matrix | `src/experiments/m6_matrix.cpp` |
| Targets | `experiments/targets/` |

## Research Position

The main research question is whether runtime control improves a greybox
fuzzing campaign after progress slows. The comparison should be against AFL++
baselines and ablations under equal budgets.

The closest boundary to keep clear:

- Harness-generation work helps create targets or drivers.
- FuzzPilot assumes the target already runs under AFL++ and controls the running
  campaign.
