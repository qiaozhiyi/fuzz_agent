# FuzzPilot MVP Run Report

Run ID: `run_1779474106432371_p139043_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `full-agent`

Plateau ID: `plateau_1779488550582320_p139043_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r02/work/run_1779474106432371_p139043_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r02/work/run_1779474106432371_p139043_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r02/work/run_1779474106432371_p139043_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r02/work/run_1779474106432371_p139043_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`195271622` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`67af2ea378fd3a3d`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`56b526c0c810e554`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`ebccd918db6cc4e2`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`02d99537f33c8674`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`8bf02d1ce82e9fd1`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`1c8a9f847d022104`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`67ff166b1bb978d2`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`b5a6462656d9d336`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`89d626dbc0db45e0`

## Micro Results

- `intv_default_1779488570755067_p139043_0035` campaign=`micro_1779488570755088_p139043_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779488570755071_p139043_0036` campaign=`micro_1779488570755110_p139043_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779488570755073_p139043_0037` campaign=`micro_1779488570755128_p139043_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779488570755075_p139043_0038` campaign=`micro_1779488570755136_p139043_0042` reward=`0` new_paths=`0` promoted=`false`
