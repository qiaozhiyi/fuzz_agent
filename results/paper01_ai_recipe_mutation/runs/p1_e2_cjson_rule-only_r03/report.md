# FuzzPilot MVP Run Report

Run ID: `run_1779503243728479_p256267_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `rule-only`

Plateau ID: `plateau_1779517719898987_p256267_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r03/work/run_1779503243728479_p256267_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r03/work/run_1779503243728479_p256267_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r03/work/run_1779503243728479_p256267_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r03/work/run_1779503243728479_p256267_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`188343851` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`fake` schema_valid=`true` context=`14bfec0c840f065f`
- `PlateauDiagnosisAgent` provider=`fake` schema_valid=`true` context=`58f6503c432bfd33`
- `SchedulerAgent` provider=`fake` schema_valid=`true` context=`58a6e098375147c5`
- `CmpAgent` provider=`fake` schema_valid=`true` context=`8565a6a4d6dd116f`
- `MutatorAgent` provider=`fake` schema_valid=`true` context=`0ec92eae96480ceb`
- `DictionaryAgent` provider=`fake` schema_valid=`true` context=`426493a6d3ef205e`
- `FormatAgent` provider=`fake` schema_valid=`true` context=`27d53f238549fec3`
- `CorpusAgent` provider=`fake` schema_valid=`true` context=`b59193749e081521`
- `ResultAnalysisAgent` provider=`fake` schema_valid=`true` context=`8a5deb085a5b1097`

## Micro Results

- `intv_default_1779517720664610_p256267_0035` campaign=`micro_1779517720664652_p256267_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779517720664625_p256267_0036` campaign=`micro_1779517720664697_p256267_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779517720664630_p256267_0037` campaign=`micro_1779517720664753_p256267_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779517720664637_p256267_0038` campaign=`micro_1779517720664797_p256267_0042` reward=`0` new_paths=`0` promoted=`false`
