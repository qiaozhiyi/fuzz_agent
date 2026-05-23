# FuzzPilot MVP Run Report

Run ID: `run_1779503243656819_p256261_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `rule-only`

Plateau ID: `plateau_1779517720638074_p256261_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r02/work/run_1779503243656819_p256261_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r02/work/run_1779503243656819_p256261_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r02/work/run_1779503243656819_p256261_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r02/work/run_1779503243656819_p256261_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`194979991` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`fake` schema_valid=`true` context=`53a8fa7e4e2231fe`
- `PlateauDiagnosisAgent` provider=`fake` schema_valid=`true` context=`529eeae8fbb8880b`
- `SchedulerAgent` provider=`fake` schema_valid=`true` context=`ed4eed044d94aa88`
- `CmpAgent` provider=`fake` schema_valid=`true` context=`ae453aadc369ca7b`
- `MutatorAgent` provider=`fake` schema_valid=`true` context=`98ea4db04d3ffa74`
- `DictionaryAgent` provider=`fake` schema_valid=`true` context=`2368d0db71ae6c0a`
- `FormatAgent` provider=`fake` schema_valid=`true` context=`dd330f9b623fe7f9`
- `CorpusAgent` provider=`fake` schema_valid=`true` context=`b718be94df3f0d8f`
- `ResultAnalysisAgent` provider=`fake` schema_valid=`true` context=`85a23c92d83781d5`

## Micro Results

- `intv_default_1779517721302618_p256261_0035` campaign=`micro_1779517721302644_p256261_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779517721302625_p256261_0036` campaign=`micro_1779517721302682_p256261_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779517721302628_p256261_0037` campaign=`micro_1779517721302714_p256261_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779517721302634_p256261_0038` campaign=`micro_1779517721302760_p256261_0042` reward=`0` new_paths=`0` promoted=`false`
