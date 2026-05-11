# FuzzPilot

FuzzPilot is a self-diagnosing, agent-driven AFL++ fuzzing controller. It keeps model API calls outside the mutator hot path so mutation performance is not blocked by online inference.

## Current status

- **Version**: v0
- **Maturity**: experimental
- **Primary development platform**: macOS Apple Silicon
- **Portability target**: Linux migration planned after the current stabilization phase

## Repository layout

- `include/` — public headers
- `src/` — core controller, runtime, telemetry, storage, CLI implementation
- `mutators/` — custom AFL++ mutator code
- `configs/` — run configuration files
- `db/` — SQLite schema and DB assets
- `docs/` — design and engineering documentation
- `experiments/` — target experiment assets and manifests
- `tests/` — fixtures and smoke-test inputs
- `tools/` — utility binaries used by tests/smoke checks

## Dependencies

Install the following tools before building:

- `cmake`
- `ninja`
- `sqlite3`
- `AFL++`
- C++20 compiler (AppleClang, Clang, or GCC)

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Common CLI examples

```bash
./build/fuzzpilot --version
./build/fuzzpilot check-config --config configs/examples/libpng.yaml
./build/fuzzpilot parse-stats --stats tests/fixtures/fuzzer_stats_newer
./build/fuzzpilot detect-plateau --older tests/fixtures/fuzzer_stats_older --newer tests/fixtures/fuzzer_stats_newer --window-sec 600
./build/fuzzpilot run --config configs/examples/libpng.yaml --work-dir build/smoke/mvp_run --schema db/schema.sql --afl-output-dir tests/fixtures/afl_out --stats tests/fixtures/fuzzer_stats_older --stats tests/fixtures/fuzzer_stats_newer --provider fake
```

## Model API configuration

Set model connection variables before running model-enabled workflows:

- `FUZZPILOT_MODEL_ENDPOINT`
- `FUZZPILOT_MODEL_API_KEY`

For local development, use an OpenAI-compatible endpoint such as Ollama, llama.cpp server, or vLLM.

## Before running against real AFL++ targets

Before non-dry-run execution, make sure your config points to:

- the real target binary
- an initialized seed corpus
- optional dictionary (`-x` equivalent) when beneficial
- the custom mutator shared object path used by your AFL++ invocation

## Experiment record recommendations

For each run, record at least:

- git commit
- config hash
- target
- seed corpus
- model config
- prompt/context hash
- agent decisions
- micro-campaign outcomes

See `docs/experiment_records.md` for the structured logging baseline.
