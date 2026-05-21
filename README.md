# FuzzPilot

FuzzPilot is an experimental C/C++ controller for AFL++ fuzzing campaigns. It
adds telemetry collection, plateau detection, model-backed planning, short
micro-campaigns, recipe-guided mutation, and optional reverse-engineering
context from IDA or Ghidra.

The core design rule is simple: model calls never run in the AFL++ custom mutator
hot path. The model observes telemetry and proposes strategy between fuzzing
windows; the mutator stays local and fast.

## Documentation

- [Project brief](docs/project_brief.md)
- [Quickstart](docs/quickstart.md)
- [Evaluation plan](docs/evaluation.md)

## Table of contents

- [Documentation](#documentation)
- [Current status](#current-status)
- [Repository layout](#repository-layout)
- [Concepts](#concepts)
- [Dependencies](#dependencies)
- [Clone and submodules](#clone-and-submodules)
- [Build](#build)
- [Ubuntu/x86 Docker smoke](#ubuntux86-docker-smoke)
- [Ubuntu/x86 cloud notes](#ubuntux86-cloud-notes)
- [Test](#test)
- [Configuration overview](#configuration-overview)
- [Quickstart: fixture-only dry run](#quickstart-fixture-only-dry-run)
- [Quickstart: cJSON AFL++ smoke](#quickstart-cjson-afl-smoke)
- [Using a real model API](#using-a-real-model-api)
- [Running full FuzzPilot loops](#running-full-fuzzpilot-loops)
- [M6 experiment matrix](#m6-experiment-matrix)
- [Recording experiment metadata](#recording-experiment-metadata)
- [Interpreting outputs](#interpreting-outputs)
- [Target development guide](#target-development-guide)
- [Security and secret handling](#security-and-secret-handling)
- [Troubleshooting](#troubleshooting)
- [Cleanup](#cleanup)
- [CI](#ci)

## Current status

- Version: `0.1.0`
- Maturity: experimental research prototype
- Primary development platform: macOS Apple Silicon
- CI platform: Linux on GitHub Actions
- Bundled real-world targets: `cJSON` and `libpng`
- Model provider support: OpenAI-compatible chat completions endpoints
- Confirmed local smoke coverage:
  - CMake build
  - CTest suite
  - process capture and env override smoke
  - JSON proposal validation smoke
  - M6 experiment matrix generation
  - dry-run orchestration
  - AFL++ cJSON smoke with the custom mutator
  - real DeepSeek/OpenAI-compatible agent smoke

## Repository layout

- `include/` - public C++ headers
- `src/` - controller, CLI, model gateway, telemetry, storage, process runner
- `mutators/` - AFL++ custom mutator implementation
- `configs/` - example configs
- `db/` - SQLite schema
- `docs/` - project brief, quickstart, and evaluation plan
- `experiments/targets/` - target harnesses, seeds, and per-target configs
- `tests/` - fixtures used by local smoke tests
- `tools/` - small helper binaries compiled for tests
- `.github/workflows/ci.yml` - Linux build/test workflow

## Concepts

FuzzPilot has a few moving pieces. Knowing these names makes the CLI output much
easier to read.

- Main campaign: the primary AFL++ process that fuzzes the target and produces
  telemetry.
- Telemetry: parsed AFL++ `fuzzer_stats` plus mutator telemetry such as recipe
  hits and misses.
- Plateau: a period where execution continues but coverage growth slows or stops.
- Blackboard: compact JSON context passed to model-backed agents. It includes
  plateau metadata, AFL metrics, static-analysis context, previous decisions, and
  agent memory.
- Agent decision: one model-backed proposal from an agent such as
  `CoordinatorAgent`, `DictionaryAgent`, or `MutatorAgent`.
- Micro-campaign: a short AFL++ run launched from a corpus snapshot to compare
  interventions such as dictionary probes or per-seed recipe probes.
- Recipe: structured mutation guidance consumed by the custom mutator.
- Promotion: selecting the best micro-campaign result and persisting its recipe
  or intervention as the next fuzzing strategy.

## Dependencies

Install these before building:

- CMake 3.22 or newer
- Ninja or another CMake generator
- SQLite3 development headers and library
- C++20 compiler
- AFL++
- Git
- Bash
- `curl`

Optional but useful:

- Ghidra headless if using `static_analysis.backend=ghidra`
- IDA Pro 9.x with `idalib` if using `static_analysis.backend=ida`
- Python environment compatible with the IDA extractor script when using IDA
- `gh` for GitHub PR and CI workflows
- `jq` or `sqlite3` for inspecting run artifacts

### macOS example

```bash
brew install cmake ninja sqlite afl++ git
```

### Ubuntu example

```bash
sudo apt-get update
sudo apt-get install -y afl++ build-essential clang cmake curl git \
  libcjson-dev libpng-dev libsqlite3-dev ninja-build pkg-config \
  python3 sqlite3 zlib1g-dev
```

## Clone and submodules

The bundled experiment targets are tracked as Git submodules/gitlinks. Clone with
submodules when starting from a fresh checkout:

```bash
git clone --recurse-submodules https://github.com/qiaozhiyi/fuzz_agent.git
cd fuzz_agent
```

If the repository was already cloned:

```bash
git submodule update --init --recursive
```

Expected target source paths:

- `experiments/targets/cjson/src`
- `experiments/targets/libpng/src`

The checked-in target binaries may have been built on a developer machine and
are not portable across CPU/OS boundaries. Before Ubuntu/x86_64 long runs,
rebuild target binaries on the server:

```bash
scripts/build_ubuntu_targets.sh
```

If `cJSON` is skipped, initialize submodules first with the command above or
install `libcjson-dev` so the harness can link against the system package.

## Build

For normal development:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The bundled configs point `mutation_strategy.custom_mutator_path` at
`./build/mutators/fuzzpilot/libfuzzpilot_mutator`. CMake creates that
extensionless link after building the platform library, so the same config works
on macOS (`.dylib`) and Ubuntu/Linux (`.so`).

Typical binaries after build:

- `build/fuzzpilot`
- `build/mutators/fuzzpilot/libfuzzpilot_mutator` cross-platform link
- `build/mutators/fuzzpilot/libfuzzpilot_mutator.so` on Linux
- `build/mutators/fuzzpilot/libfuzzpilot_mutator.dylib` on macOS
- `build/fuzzpilot_mutator_smoke`
- `build/fuzzpilot_process_capture_smoke`
- `build/fuzzpilot_json_utils_smoke`

## Ubuntu/x86 Docker smoke

On Apple Silicon, this checks the Ubuntu path under `linux/amd64` so it is close
to the long-running x86_64 cloud server environment:

```bash
scripts/docker_ubuntu_smoke.sh
```

The script builds `docker/ubuntu/Dockerfile`, compiles into
`/tmp/fuzzpilot-build` inside the container, runs CTest, verifies both the Linux
`.so` mutator and the extensionless `libfuzzpilot_mutator` link, and previews the
cJSON AFL++ command. It also rebuilds `vuln_target`, `cJSON`, and `libpng` in a
temporary source copy and checks that the rebuilt binaries are ELF/x86-64.

On an x86_64 Ubuntu cloud server, the same Docker smoke works without emulation.
For native runs, use the Ubuntu packages above, then build with the normal CMake
commands.

## Ubuntu/x86 Cloud Notes

Use [experiments/README.md](experiments/README.md) when moving to a long-running
x86_64 Ubuntu server. The important rule is to rebuild all target binaries on
the server and confirm they are `ELF 64-bit` `x86-64` before starting M6 runs.

## Test

Run the full local test suite:

```bash
ctest --test-dir build --output-on-failure
```

Useful individual checks:

```bash
./build/fuzzpilot --version
./build/fuzzpilot check-config --config configs/examples/libpng.yaml
./build/fuzzpilot parse-stats --stats tests/fixtures/fuzzer_stats_newer
./build/fuzzpilot detect-plateau \
  --older tests/fixtures/fuzzer_stats_older \
  --newer tests/fixtures/fuzzer_stats_newer \
  --window-sec 600
```

Validate the metadata script:

```bash
bash -n scripts/capture_run_metadata.sh
```

## Configuration overview

Each run is driven by a YAML-like config. The parser is intentionally lightweight,
so prefer simple key/value structures like the checked-in examples.

Important sections:

```yaml
project: "cJSON_fuzz"

target:
  name: "cjson_parser"
  binary: "experiments/targets/cjson/cjson_fuzzer"
  input_dir: "experiments/targets/cjson/seeds"
  args: ["@@"]
  timeout_ms: 1000
  memory_mb: 1024

afl:
  base_env:
    AFL_MAP_SIZE: "4096"
    AFL_NO_UI: "1"
    AFL_SKIP_CPUFREQ: "1"
  main_budget_sec: 120
  plateau_window_sec: 10

micro_campaign:
  budget_sec: 20
  num_candidates: 4

mutation_strategy:
  enabled: true
  recipe_store: "work_cjson/recipes"
  custom_mutator_path: "./build/mutators/fuzzpilot/libfuzzpilot_mutator"

static_analysis:
  enabled: false
  backend: "ghidra"
  ghidra_home: "/opt/ghidra"
  extractor_script: "scripts/ghidra/FuzzPilotGhidraExtract.java"
  timeout_sec: 120

model_api:
  provider: "openai-compatible"
  endpoint: "https://api.deepseek.com/chat/completions"
  model: "deepseek-chat"
  api_key_env: "FUZZPILOT_MODEL_API_KEY"
```

Config notes:

- `target.args` should include `@@` when the target reads from a file path.
- `afl.base_env.AFL_MAP_SIZE` must match the instrumented target when AFL++ asks
  for a non-default map size.
- `mutation_strategy.custom_mutator_path` can point at the extensionless
  `libfuzzpilot_mutator` link, or directly at the platform `.so`/`.dylib`.
- `model_api.api_key_env` is the name of the environment variable, not the secret
  value.
- `model_api.provider` accepts `openai-compatible` and legacy
  `openai_compatible`.
- `target.dict` and legacy `target.dictionary` are both supported.
- `model.model_name` and `model_api.model` are both supported.
- `static_analysis.backend` accepts `ghidra` or `ida`; use `ghidra` on
  Ubuntu/x86_64 servers and keep `enabled: false` until the backend is installed.

## Quickstart: fixture-only dry run

This path does not launch AFL++. It replays fixture stats and is the fastest way
to verify the controller, database, plateau detection, micro-campaign planning,
agent runtime, and report writing.

```bash
./build/fuzzpilot run \
  --config configs/examples/libpng.yaml \
  --work-dir build/smoke/mvp_run \
  --schema db/schema.sql \
  --afl-output-dir tests/fixtures/afl_out \
  --stats tests/fixtures/fuzzer_stats_older \
  --stats tests/fixtures/fuzzer_stats_newer \
  --micro-stats tests/fixtures/fuzzer_stats_micro_control \
  --micro-stats tests/fixtures/fuzzer_stats_micro_dictionary \
  --micro-stats tests/fixtures/fuzzer_stats_micro_seed_focus \
  --micro-stats tests/fixtures/fuzzer_stats_micro_winner \
  --provider fake
```

Expected artifacts:

- `build/smoke/mvp_run/<run_id>/report.md`
- `coverage.csv`
- `events.jsonl`
- `agent_decisions.jsonl`
- `agent_memory.jsonl`
- `fuzzpilot.sqlite`

## Quickstart: cJSON AFL++ smoke

This launches AFL++ for a short bounded run. Use it to confirm target execution,
AFL++ instrumentation, the custom mutator, and output directory creation.

```bash
mkdir -p /tmp/fuzzpilot_cjson_recipes

AFL_CUSTOM_MUTATOR_LIBRARY=./build/mutators/fuzzpilot/libfuzzpilot_mutator \
AFL_MAP_SIZE=4096 \
AFL_NO_UI=1 \
AFL_SKIP_CPUFREQ=1 \
FUZZPILOT_RECIPE_STORE=/tmp/fuzzpilot_cjson_recipes \
afl-fuzz -V 5 \
  -i experiments/targets/cjson/seeds \
  -o /tmp/fuzzpilot_cjson_afl_smoke \
  -m 1024 \
  -t 1000 \
  -- experiments/targets/cjson/cjson_fuzzer @@
```

Healthy signs:

- AFL++ loads the custom mutator successfully.
- The fork server starts.
- The seed dry run succeeds.
- AFL++ reports new corpus items or at least stable execution.
- Crashes and timeouts remain zero for a basic smoke.

## Using a real model API

FuzzPilot supports OpenAI-compatible chat completions APIs. DeepSeek works with
the checked-in target configs.

Never put a real API key in a config, README, commit, shell history, issue, or
PR comment. Export it only in your local shell:

```bash
export FUZZPILOT_MODEL_API_KEY="<YOUR_API_KEY>"
```

Optional endpoint override:

```bash
export FUZZPILOT_MODEL_ENDPOINT="https://api.deepseek.com/chat/completions"
```

Minimal real API smoke:

```bash
./build/fuzzpilot run-model-agents \
  --db /tmp/fuzzpilot_model_probe.sqlite \
  --schema db/schema.sql \
  --run-id run_model_probe \
  --plateau-id plateau_model_probe \
  --blackboard-json '{"plateau":{"reason":"api_probe"},"target":{"name":"cjson_parser","format":"JSON"},"main_metrics":{"execs_done":1000,"execs_per_sec":1000},"static_analysis_context":{"magic_tokens":["true","false","null","[","]","{","}"]}}' \
  --provider openai-compatible \
  --endpoint https://api.deepseek.com/chat/completions \
  --model deepseek-chat \
  --api-key-env FUZZPILOT_MODEL_API_KEY
```

Inspect the results:

```bash
sqlite3 /tmp/fuzzpilot_model_probe.sqlite \
  'select agent, schema_valid, fallback_used, latency_ms from agent_decisions order by agent;'
```

Expected result:

- one row per agent
- `schema_valid` should be `1`
- `fallback_used` should be `0`
- latency values should be nonzero

If a model response is truncated or malformed, FuzzPilot now marks
`schema_valid=0` instead of silently accepting partial JSON.

## Running full FuzzPilot loops

### Fake-provider controller run

Use this when validating local orchestration without spending API credits:

```bash
./build/fuzzpilot run \
  --config experiments/targets/cjson/config.yaml \
  --work-dir work_cjson_fake \
  --schema db/schema.sql \
  --provider fake
```

### Real model run

Use this when you are ready to run AFL++ plus real agent decisions:

```bash
export FUZZPILOT_MODEL_API_KEY="<YOUR_API_KEY>"

./build/fuzzpilot run \
  --config experiments/targets/cjson/config.yaml \
  --work-dir work_cjson_deepseek \
  --schema db/schema.sql \
  --real-run
```

For libpng:

```bash
export FUZZPILOT_MODEL_API_KEY="<YOUR_API_KEY>"

./build/fuzzpilot run \
  --config experiments/targets/libpng/config.yaml \
  --work-dir work_libpng_deepseek \
  --schema db/schema.sql \
  --real-run
```

Operational notes:

- `--real-run` launches AFL++ instead of replaying fixture stats.
- The main AFL++ run collects telemetry until budget or plateau.
- On plateau, FuzzPilot snapshots the corpus, runs static analysis if enabled,
  asks agents for strategies, then launches micro-campaigns.
- The controller stops the main AFL++ process before micro-campaigns so AFL++ can
  release shared memory cleanly on macOS.
- Reports are written under the selected `--work-dir`.

## M6 experiment matrix

M6 is the real-target validation phase. Generate the reproducible target/mode
matrix before launching long runs:

```bash
./build/fuzzpilot m6-matrix \
  --config experiments/targets/cjson/config.yaml \
  --config experiments/targets/libpng/config.yaml \
  --out-dir results/m6_matrix \
  --work-dir work_m6 \
  --repeats 3 \
  --main-budget-sec 86400 \
  --micro-budget-sec 300
```

This writes:

- `results/m6_matrix/m6_matrix.json`
- `results/m6_matrix/m6_matrix.md`

Each target is planned across these modes:

- `baseline-afl` - plain AFL++ control, no model agents, static analysis, or custom mutator
- `rule-only` - deterministic fallback agents with micro-campaign validation
- `no-static-analysis` - full loop without reverse-engineering intelligence
- `no-mutator` - full loop without custom mutator recipes
- `full-agent` - complete configured loop

The `run` command accepts the same ablation modes directly:

```bash
./build/fuzzpilot run \
  --config experiments/targets/cjson/config.yaml \
  --work-dir work_m6/cjson_parser/full-agent/r1 \
  --schema db/schema.sql \
  --real-run \
  --ablation full-agent \
  --main-budget-sec 86400 \
  --micro-budget-sec 300
```

## Recording experiment metadata

Use the metadata helper before or after important runs:

```bash
scripts/capture_run_metadata.sh \
  --run-id run_cjson_001 \
  --config experiments/targets/cjson/config.yaml \
  --target cjson_parser \
  --out-dir results/run_cjson_001
```

The script writes:

- `run_metadata.json` - commit, branch, config hash, target, OS, arch
- `git_status.txt` - branch and dirty worktree summary
- `git.patch` - staged and unstaged local diff

Recommended run bundle:

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
```

See [docs/evaluation.md](docs/evaluation.md) for the required field baseline.

## Interpreting outputs

Common files:

- `report.md` - human-readable summary of the run
- `coverage.csv` - time series of AFL++ telemetry
- `events.jsonl` - append-only machine-readable event log
- `agent_decisions.jsonl` - model/fake agent decisions
- `agent_memory.jsonl` - memory patches persisted from agent results
- `fuzzpilot.sqlite` - normalized run, telemetry, campaign, plateau, and agent data
- `main_launch.sh` - reproducible main AFL++ command preview
- `micro/` - micro-campaign directories
- `promoted_recipes/` - selected recipes after intervention evaluation

Useful SQLite queries:

```bash
sqlite3 work_cjson_deepseek/<run_id>/fuzzpilot.sqlite \
  'select agent, schema_valid, fallback_used, latency_ms from agent_decisions order by created_ts;'

sqlite3 work_cjson_deepseek/<run_id>/fuzzpilot.sqlite \
  'select campaign_id, execs_done, paths_total, bitmap_cvg, unique_crashes from telemetry order by ts desc limit 10;'
```

## Target development guide

A target directory should contain:

```text
experiments/targets/<name>/
  config.yaml
  harness.c or harness.cpp
  <target_fuzzer>
  seeds/
    seed1.*
  src/
```

Target checklist:

- Build the harness with AFL++ instrumentation.
- Confirm the harness exits cleanly on every seed.
- Keep seed files small but structurally valid.
- Set `target.args` to `["@@"]` if the harness expects a file path.
- Use `target.timeout_ms` to catch hangs without killing normal parsing.
- Set `target.memory_mb` high enough for legitimate target allocations.
- Set `AFL_MAP_SIZE` if AFL++ reports a required map size.
- Add a dictionary when the format has stable magic tokens.
- Keep crash-triggering or generated outputs out of Git unless they are tiny,
  intentional fixtures.

Direct seed checks:

```bash
experiments/targets/cjson/cjson_fuzzer experiments/targets/cjson/seeds/seed1.json
experiments/targets/libpng/libpng_fuzzer experiments/targets/libpng/seeds/seed1.png
```

## Security and secret handling

Rules for API keys:

- Do not commit secrets.
- Do not paste real secrets into README examples.
- Do not store secrets in `config.yaml`.
- Prefer `api_key_env: "FUZZPILOT_MODEL_API_KEY"`.
- Export the secret only in the local shell that runs FuzzPilot.
- Rotate the key if it appears in a commit, issue, PR, log, or screenshot.

Rules for command execution:

- FuzzPilot uses argv-based process execution for model `curl`, environment
  capture, and static-analysis helper paths.
- Model API secrets are passed to `curl` through a private temporary header file,
  not directly in process arguments.
- Captured subprocess output is bounded and model/static-analysis helper calls have timeouts.
- Environment variable names are validated before use.
- Model responses must be complete agent proposal JSON before being marked
  `schema_valid=true`.
- Invalid or truncated proposal text is serialized as raw text instead of being
  embedded as broken JSON.
- Corpus snapshots skip symlinks and cap copied queue files to protect local
  storage during long M6 runs.
- Compact recipes reject control-character tokens so model or CLI output cannot
  inject extra recipe directives through dictionary token text.

## Troubleshooting

### `target binary does not exist`

Build the target harness or update `target.binary` in the config.

### `custom mutator library not found`

Build the project in the directory referenced by
`mutation_strategy.custom_mutator_path`, or update the config.

### AFL++ asks for a different map size

Set `afl.base_env.AFL_MAP_SIZE` in the config to the value AFL++ reports.

### macOS shared memory errors

Stop stale AFL++ processes and rerun. FuzzPilot stops the main AFL++ process
before micro-campaigns to reduce shared memory pressure, but interrupted manual
runs can still leave state behind.

### Model requests fail

Check:

- `FUZZPILOT_MODEL_API_KEY` is exported in the current shell.
- The endpoint is reachable.
- The model name is valid for the provider.
- The provider is `openai-compatible`.
- `run-model-agents` works before attempting a full `--real-run`.

### `schema_valid=0`

This means the model response was not a complete agent proposal JSON object. It
may be truncated, off-schema, or an API error body. Reduce prompt size, provide
more focused blackboard context, or increase provider-side output limits if the
provider supports it.

### GitHub Actions checkout annotation about submodules

Ensure `.gitmodules` contains entries for target source gitlinks:

- `experiments/targets/cjson/src`
- `experiments/targets/libpng/src`

## Cleanup

Check for fuzzing leftovers:

```bash
pgrep -af 'afl-fuzz|fuzzpilot run|cjson_fuzzer|libpng_fuzzer'
```

Stop a known leftover process:

```bash
kill <pid>
```

Temporary local run directories are usually safe to remove when you no longer
need their reports:

```bash
rm -rf work_cjson_fake work_cjson_deepseek work_libpng_deepseek
rm -rf /tmp/fuzzpilot_*
```

Do not delete artifacts that you still need for experiment reproducibility.

## CI

GitHub Actions runs on push and pull request:

```text
.github/workflows/ci.yml
```

The workflow installs CMake, Ninja, SQLite, and a C++ compiler, then runs:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

CI intentionally does not require AFL++ or real model credentials. Those are
validated with local smoke tests and explicit real-run experiments.
