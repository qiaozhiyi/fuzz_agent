# FuzzPilot MVP Run Report

Run ID: `run_1779474106514642_p139046_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `full-agent`

Plateau ID: `plateau_1779488552469854_p139046_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r03/work/run_1779474106514642_p139046_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r03/work/run_1779474106514642_p139046_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r03/work/run_1779474106514642_p139046_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_full-agent_r03/work/run_1779474106514642_p139046_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`195247045` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`d97cb460170c8ed6`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`2d7423a667fb3893`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`078e978b093b9e85`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`5e94ebb4a8c77df5`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`ab33169cbb30ffaa`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`1e9a6e62a1ab2010`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`91983f7d6096db55`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`c668c9139451c4a9`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`8b4d939a7e72e918`

## Micro Results

- `intv_default_1779488577470069_p139046_0035` campaign=`micro_1779488577470113_p139046_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779488577470076_p139046_0036` campaign=`micro_1779488577470155_p139046_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779488577470080_p139046_0037` campaign=`micro_1779488577470190_p139046_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779488577470084_p139046_0038` campaign=`micro_1779488577470212_p139046_0042` reward=`0` new_paths=`0` promoted=`false`
