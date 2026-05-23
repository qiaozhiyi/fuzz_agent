# FuzzPilot MVP Run Report

Run ID: `run_1779474106841217_p139049_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `full-agent`

Plateau ID: `plateau_1779488549605347_p139049_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r01/work/run_1779474106841217_p139049_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r01/work/run_1779474106841217_p139049_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r01/work/run_1779474106841217_p139049_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r01/work/run_1779474106841217_p139049_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`199266586` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`06131c9385d038ce`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`15c613ed43c22678`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`8d8f95bfbd36864f`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`472a0eba9dde28c4`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`f94deaa87048c9fb`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`b0b75f8790dd521f`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`7432c50e5a741961`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`766c0da0a3af2aeb`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`995dff37a28b36f1`

## Micro Results

- `intv_default_1779488568323159_p139049_0035` campaign=`micro_1779488568323187_p139049_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779488568323164_p139049_0036` campaign=`micro_1779488568323228_p139049_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779488568323173_p139049_0037` campaign=`micro_1779488568323251_p139049_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779488568323176_p139049_0038` campaign=`micro_1779488568323267_p139049_0042` reward=`0` new_paths=`0` promoted=`false`
