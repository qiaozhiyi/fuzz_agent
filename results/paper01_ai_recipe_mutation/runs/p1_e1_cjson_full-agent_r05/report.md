# FuzzPilot MVP Run Report

Run ID: `run_1779488665556782_p230982_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `full-agent`

Plateau ID: `plateau_1779503103903241_p230982_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r05/work/run_1779488665556782_p230982_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r05/work/run_1779488665556782_p230982_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r05/work/run_1779488665556782_p230982_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r05/work/run_1779488665556782_p230982_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`224996600` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`d67251f1000b1318`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`441764587f48bccb`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`8858840934d59527`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`90dd70c5ba5bd7a9`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`cc089e1c747ab766`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`false` context=`2533727a4c6c2533`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`ee00004fbfc4742a`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`f035a5696dd633f6`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`d88d761bcef5ae30`

## Micro Results

- `intv_default_1779503137705013_p230982_0035` campaign=`micro_1779503137705057_p230982_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779503137705028_p230982_0036` campaign=`micro_1779503137705117_p230982_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779503137705033_p230982_0037` campaign=`micro_1779503137705151_p230982_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779503137705036_p230982_0038` campaign=`micro_1779503137705186_p230982_0042` reward=`0` new_paths=`0` promoted=`false`
