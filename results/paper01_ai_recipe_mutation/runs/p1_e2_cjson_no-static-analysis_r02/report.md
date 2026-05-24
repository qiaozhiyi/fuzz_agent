# FuzzPilot MVP Run Report

Run ID: `run_1779544877513399_p435835_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-static-analysis`

Plateau ID: `plateau_1779559363787535_p435835_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r02/work/run_1779544877513399_p435835_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r02/work/run_1779544877513399_p435835_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r02/work/run_1779544877513399_p435835_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-static-analysis_r02/work/run_1779544877513399_p435835_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`148133381` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`ddd2489a27588d0c`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`339e06db91af464e`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`ed29c57b6f429c1e`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`b4b055a71d2233ef`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`682c53882b36ca91`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`true` context=`c7c4797c286602e5`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`72d69ca6a2a0c61e`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`648d938597db2479`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`ddba78353f44c57c`

## Micro Results

- `intv_default_1779559397849326_p435835_0035` campaign=`micro_1779559397849360_p435835_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779559397849334_p435835_0036` campaign=`micro_1779559397849397_p435835_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779559397849338_p435835_0037` campaign=`micro_1779559397849421_p435835_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779559397849343_p435835_0038` campaign=`micro_1779559397849443_p435835_0042` reward=`0` new_paths=`0` promoted=`false`
