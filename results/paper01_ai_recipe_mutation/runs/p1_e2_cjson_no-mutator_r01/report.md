# FuzzPilot MVP Run Report

Run ID: `run_1779517827844792_p318620_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `no-mutator`

Plateau ID: `plateau_1779532310045614_p318620_0001`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r01/work/run_1779517827844792_p318620_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r01/work/run_1779517827844792_p318620_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r01/work/run_1779517827844792_p318620_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r01/work/run_1779517827844792_p318620_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`182293695` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`openai_compatible` schema_valid=`true` context=`9849df5666fe26b1`
- `PlateauDiagnosisAgent` provider=`openai_compatible` schema_valid=`true` context=`60c572dff016a052`
- `SchedulerAgent` provider=`openai_compatible` schema_valid=`true` context=`3ada0691e1a3dae5`
- `CmpAgent` provider=`openai_compatible` schema_valid=`true` context=`34f71188ab3b64c1`
- `MutatorAgent` provider=`openai_compatible` schema_valid=`true` context=`3d5f99df8760d7eb`
- `DictionaryAgent` provider=`openai_compatible` schema_valid=`false` context=`2a0513def9d81d1b`
- `FormatAgent` provider=`openai_compatible` schema_valid=`true` context=`35ea060178a1611b`
- `CorpusAgent` provider=`openai_compatible` schema_valid=`true` context=`fad984b269ab33dc`
- `ResultAnalysisAgent` provider=`openai_compatible` schema_valid=`true` context=`fdb75e20bbcd1993`

## Micro Results

- `intv_default_1779532355315922_p318620_0034` campaign=`micro_1779532355315955_p318620_0038` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779532355315931_p318620_0035` campaign=`micro_1779532355316030_p318620_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779532355315937_p318620_0036` campaign=`micro_1779532355316048_p318620_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779532355315941_p318620_0037` campaign=`micro_1779532355316059_p318620_0041` reward=`0` new_paths=`0` promoted=`false`
