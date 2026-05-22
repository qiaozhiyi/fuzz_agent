# FuzzPilot M6 Experiment Matrix

- targets: `2`
- repeats per mode: `1`
- main budget seconds: `20`
- micro budget seconds: `5`

## cjson_parser

- config: `experiments/targets/cjson/config.yaml`
- binary: `experiments/targets/cjson/cjson_fuzzer`
- seeds: `experiments/targets/cjson/seeds`
- audit: `clean`

```bash
./build/fuzzpilot run --config experiments/targets/cjson/config.yaml --work-dir work_paper01_ai_recipe_mutation/cjson_parser/baseline-afl/r1 --schema db/schema.sql --real-run --ablation baseline-afl --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/cjson/config.yaml --work-dir work_paper01_ai_recipe_mutation/cjson_parser/rule-only/r1 --schema db/schema.sql --real-run --ablation rule-only --main-budget-sec 20 --micro-budget-sec 5 --provider fake
```

```bash
./build/fuzzpilot run --config experiments/targets/cjson/config.yaml --work-dir work_paper01_ai_recipe_mutation/cjson_parser/no-static-analysis/r1 --schema db/schema.sql --real-run --ablation no-static-analysis --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/cjson/config.yaml --work-dir work_paper01_ai_recipe_mutation/cjson_parser/no-mutator/r1 --schema db/schema.sql --real-run --ablation no-mutator --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/cjson/config.yaml --work-dir work_paper01_ai_recipe_mutation/cjson_parser/full-agent/r1 --schema db/schema.sql --real-run --ablation full-agent --main-budget-sec 20 --micro-budget-sec 5
```

## libpng_parser

- config: `experiments/targets/libpng/config.yaml`
- binary: `experiments/targets/libpng/libpng_fuzzer`
- seeds: `experiments/targets/libpng/seeds`
- audit: `clean`

```bash
./build/fuzzpilot run --config experiments/targets/libpng/config.yaml --work-dir work_paper01_ai_recipe_mutation/libpng_parser/baseline-afl/r1 --schema db/schema.sql --real-run --ablation baseline-afl --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/libpng/config.yaml --work-dir work_paper01_ai_recipe_mutation/libpng_parser/rule-only/r1 --schema db/schema.sql --real-run --ablation rule-only --main-budget-sec 20 --micro-budget-sec 5 --provider fake
```

```bash
./build/fuzzpilot run --config experiments/targets/libpng/config.yaml --work-dir work_paper01_ai_recipe_mutation/libpng_parser/no-static-analysis/r1 --schema db/schema.sql --real-run --ablation no-static-analysis --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/libpng/config.yaml --work-dir work_paper01_ai_recipe_mutation/libpng_parser/no-mutator/r1 --schema db/schema.sql --real-run --ablation no-mutator --main-budget-sec 20 --micro-budget-sec 5
```

```bash
./build/fuzzpilot run --config experiments/targets/libpng/config.yaml --work-dir work_paper01_ai_recipe_mutation/libpng_parser/full-agent/r1 --schema db/schema.sql --real-run --ablation full-agent --main-budget-sec 20 --micro-budget-sec 5
```

