# Quickstart

This is the shortest path to check a clean checkout.

## Clone

```bash
git clone --recurse-submodules https://github.com/qiaozhiyi/fuzz_agent.git
cd fuzz_agent
```

If already cloned:

```bash
git submodule update --init --recursive
```

## Dependencies

macOS:

```bash
brew install cmake ninja sqlite afl++ git
```

Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y afl++ build-essential clang cmake curl git \
  libcjson-dev libpng-dev libsqlite3-dev ninja-build pkg-config \
  python3 sqlite3 zlib1g-dev
```

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Config Check

```bash
./build/fuzzpilot check-config \
  --config experiments/targets/cjson/config.yaml \
  --runtime
```

## AFL++ Command Preview

```bash
./build/fuzzpilot afl-command \
  --config experiments/targets/cjson/config.yaml \
  --output-dir /tmp/fuzzpilot-cjson-afl \
  --recipe-store /tmp/fuzzpilot-cjson-recipes
```

Check that the command includes:

- `AFL_CUSTOM_MUTATOR_LIBRARY`
- `FUZZPILOT_RECIPE_STORE`
- the target binary
- `@@` if the target expects a file path

## Fixture Smoke

This does not launch AFL++. It replays checked-in stats and verifies the
controller path.

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

Use an environment variable for the key. Do not put a key in a config file.

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
