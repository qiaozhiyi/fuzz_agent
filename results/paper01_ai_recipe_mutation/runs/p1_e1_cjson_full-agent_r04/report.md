# FuzzPilot MVP Run Report

Run ID: `run_1779474106370186_p139040_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `full-agent`

Plateau ID: `plateau_1779488551733601_p139040_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r04/work/run_1779474106370186_p139040_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r04/work/run_1779474106370186_p139040_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r04/work/run_1779474106370186_p139040_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r04/work/run_1779474106370186_p139040_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`201127516` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`ad18863a5bcf846c`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`2642ee0defc5a50c`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`3c8576ac15405fa1`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`0d9748dccd2e34df`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`ab594900c03c15f5`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`false` context=`d61668ebef6ecbda`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`816ae154e915533d`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`35d33b4e6f5cd791`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`bf10ba1fa3a5d7f3`

## Micro Results

- `intv_default_1779488577998180_p139040_0035` campaign=`micro_1779488577998206_p139040_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779488577998190_p139040_0036` campaign=`micro_1779488577998250_p139040_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779488577998192_p139040_0037` campaign=`micro_1779488577998268_p139040_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779488577998195_p139040_0038` campaign=`micro_1779488577998287_p139040_0042` reward=`0` new_paths=`0` promoted=`false`
