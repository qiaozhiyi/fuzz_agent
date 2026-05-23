# FuzzPilot MVP Run Report

Run ID: `run_1779517826711816_p318614_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-mutator`

Plateau ID: `plateau_1779532308891860_p318614_0001`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r02/work/run_1779517826711816_p318614_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r02/work/run_1779517826711816_p318614_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r02/work/run_1779517826711816_p318614_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r02/work/run_1779517826711816_p318614_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`183374423` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`0a67c8de7dc5c39a`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`2964dcdb92759631`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`9db6d734c73aeba0`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`f38efff7ae070cf3`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`c16b1d986d7054eb`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`0bd9276cd6d15c68`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`97700154a808f09b`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`375d2ee0855c311e`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`b6f7090d22eb0903`

## Micro Results

- `intv_default_1779532338107855_p318614_0034` campaign=`micro_1779532338107885_p318614_0038` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779532338107863_p318614_0035` campaign=`micro_1779532338107959_p318614_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779532338107867_p318614_0036` campaign=`micro_1779532338107994_p318614_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779532338107873_p318614_0037` campaign=`micro_1779532338108008_p318614_0041` reward=`0` new_paths=`0` promoted=`false`
