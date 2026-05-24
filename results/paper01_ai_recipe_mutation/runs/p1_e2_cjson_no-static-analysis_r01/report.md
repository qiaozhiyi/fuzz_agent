# FuzzPilot MVP Run Report

Run ID: `run_1779544877530291_p435833_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-static-analysis`

Plateau ID: `plateau_1779559366876538_p435833_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r01/work/run_1779544877530291_p435833_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r01/work/run_1779544877530291_p435833_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r01/work/run_1779544877530291_p435833_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r01/work/run_1779544877530291_p435833_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`147945766` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`25c5475858ba6349`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`7da9109fc58909d7`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`714a31b811de4549`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`73ae79a5885955bf`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`34811fcab64539a2`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`false` context=`7aeba90d047ceebc`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`fb2406678ac933b0`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`86715d7074e5f253`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`2c2f52a691875783`

## Micro Results

- `intv_default_1779559405957928_p435833_0035` campaign=`micro_1779559405957997_p435833_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779559405957937_p435833_0036` campaign=`micro_1779559405958062_p435833_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779559405957942_p435833_0037` campaign=`micro_1779559405958106_p435833_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779559405957949_p435833_0038` campaign=`micro_1779559405958136_p435833_0042` reward=`0` new_paths=`0` promoted=`false`
