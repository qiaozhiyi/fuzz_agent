# Quickstart

This is the shortest path to check a clean checkout through Docker. The host
only needs Docker Buildx; AFL++, Ghidra, Python packages, and target sources are
prepared inside the image.

## Clone

```bash
git clone --recurse-submodules https://github.com/qiaozhiyi/fuzz_agent.git
cd fuzz_agent
```

Submodules are optional for Docker runs; the image clones pinned cJSON/libpng
revisions during build.

## Docker Build

```bash
scripts/fuzzpilot_docker.sh build
```

## Preflight

```bash
scripts/fuzzpilot_docker.sh preflight
```

## Smoke

```bash
scripts/fuzzpilot_docker.sh smoke
```

Smoke artifacts are written under `results/docker_smoke/`.

## Paper-Canonical Platform

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh smoke
```

## Native Development

Native builds are still useful for editing and unit tests:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Do not publish paper numbers from mixed host builds; use Docker for experiment
data.

## Fixture Smoke

```bash
./build/fuzzpilot run \
  --config configs/examples/libpng.yaml \
  --work-dir build/smoke/fixture \
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

Expected files under `build/smoke/fixture/<run_id>/`:

- `report.md`
- `coverage.csv`
- `events.jsonl`
- `agent_decisions.jsonl`
- `agent_memory.jsonl`
- `fuzzpilot.sqlite`

## Optional Model Smoke

Use an environment variable for the key. Do not put a key in a config file. For
Docker runs, pass the key through the wrapper:

```bash
FUZZPILOT_MODEL_API_KEY="<YOUR_API_KEY>" \
  scripts/fuzzpilot_docker.sh run-batch --exp E1b --repeats 1 --budget-sec 60
```

For native development:

```bash
export FUZZPILOT_MODEL_API_KEY="<YOUR_API_KEY>"

./build/fuzzpilot run-model-agents \
  --db /tmp/fuzzpilot_model_probe.sqlite \
  --schema db/schema.sql \
  --run-id run_model_probe \
  --plateau-id plateau_model_probe \
  --blackboard-json '{"plateau":{"reason":"api_probe"},"target":{"name":"cjson_parser","format":"JSON"},"main_metrics":{"execs_done":1000,"execs_per_sec":1000}}' \
  --provider openai-compatible \
  --endpoint https://api.deepseek.com/chat/completions \
  --model deepseek-chat \
  --api-key-env FUZZPILOT_MODEL_API_KEY
```

Inspect:

```bash
sqlite3 /tmp/fuzzpilot_model_probe.sqlite \
  'select agent, schema_valid, fallback_used, latency_ms from agent_decisions order by agent;'
```
