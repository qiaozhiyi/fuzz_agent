# FuzzPilot MVP Run Report

Run ID: `run_1779517826980444_p318617_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-mutator`

Plateau ID: `plateau_1779532307541070_p318617_0001`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r03/work/run_1779517826980444_p318617_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r03/work/run_1779517826980444_p318617_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r03/work/run_1779517826980444_p318617_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r03/work/run_1779517826980444_p318617_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`184452920` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`bdd8a6440dd19a3f`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`08e97286d6efa01d`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`3dfda28d766ca0e1`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`9f52beeb08c379f0`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`88556e85f091b12f`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`cd2eac1fdd17172e`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`da3c3ee05e09b27d`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`6d916bd22f202ff4`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`a6e7bc561497319e`

## Micro Results

- `intv_default_1779532331935304_p318617_0034` campaign=`micro_1779532331935365_p318617_0038` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779532331935324_p318617_0035` campaign=`micro_1779532331935418_p318617_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779532331935336_p318617_0036` campaign=`micro_1779532331935461_p318617_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779532331935343_p318617_0037` campaign=`micro_1779532331935496_p318617_0041` reward=`0` new_paths=`0` promoted=`false`
