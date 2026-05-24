# FuzzPilot MVP Run Report

Run ID: `run_1779544877720904_p435837_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-static-analysis`

Plateau ID: `plateau_1779559362811870_p435837_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r03/work/run_1779544877720904_p435837_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r03/work/run_1779544877720904_p435837_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r03/work/run_1779544877720904_p435837_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r03/work/run_1779544877720904_p435837_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`154466377` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`ca73ca8c8dec6b7c`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`e36fd5463429ece8`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`45ee8fa0a7253120`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`e0ae28fd1255ef54`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`00d77d498d40ddca`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`10b537dae6d90600`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`ba3c917961ad1b8c`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`7987038263b75cbe`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`19d731dc9b93c3ff`

## Micro Results

- `intv_default_1779559386864907_p435837_0035` campaign=`micro_1779559386864954_p435837_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779559386864915_p435837_0036` campaign=`micro_1779559386864987_p435837_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779559386864932_p435837_0037` campaign=`micro_1779559386865052_p435837_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779559386864937_p435837_0038` campaign=`micro_1779559386865083_p435837_0042` reward=`0` new_paths=`0` promoted=`false`
