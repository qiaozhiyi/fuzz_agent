# FuzzPilot MVP Run Report

Run ID: `run_1779503243675460_p256264_0000`

Project: `cJSON_fuzz`

Target: `cjson_parser`

Ablation mode: `rule-only`

Plateau ID: `plateau_1779517719201349_p256264_0002`

Main AFL launch plan: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r01/work/run_1779503243675460_p256264_0000/main_launch.sh`

Main AFL PID: `-1`

Coverage CSV: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r01/work/run_1779503243675460_p256264_0000/coverage.csv`

Agent replay log: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r01/work/run_1779503243675460_p256264_0000/agent_decisions.jsonl`

Agent memory: `/root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_rule-only_r01/work/run_1779503243675460_p256264_0000/agent_memory.jsonl`

Winner intervention: ``

Winner reward: `0`

Promoted recipe index: ``

## Coverage

- samples=`14400` execs_delta=`195350260` paths_delta=`0` crashes=`0`

## Agent Decisions

- `CoordinatorAgent` provider=`fake` schema_valid=`true` context=`abde9522cef1dc43`
- `PlateauDiagnosisAgent` provider=`fake` schema_valid=`true` context=`2dc459a7cd6ef93a`
- `SchedulerAgent` provider=`fake` schema_valid=`true` context=`a6d8ea6914220782`
- `CmpAgent` provider=`fake` schema_valid=`true` context=`0bcb000c9f705e0d`
- `MutatorAgent` provider=`fake` schema_valid=`true` context=`401049abd168e7e7`
- `DictionaryAgent` provider=`fake` schema_valid=`true` context=`2fa60d2e6d09e84f`
- `FormatAgent` provider=`fake` schema_valid=`true` context=`50fe4f3f18271425`
- `CorpusAgent` provider=`fake` schema_valid=`true` context=`118c0960ddb30c4c`
- `ResultAnalysisAgent` provider=`fake` schema_valid=`true` context=`adef472c5f7d525d`

## Micro Results

- `intv_default_1779517720159029_p256264_0035` campaign=`micro_1779517720159073_p256264_0039` reward=`0` new_paths=`0` promoted=`false`
- `intv_dictionary_1779517720159049_p256264_0036` campaign=`micro_1779517720159101_p256264_0040` reward=`0` new_paths=`0` promoted=`false`
- `intv_seed_focus_1779517720159058_p256264_0037` campaign=`micro_1779517720159136_p256264_0041` reward=`0` new_paths=`0` promoted=`false`
- `intv_per_seed_recipe_1779517720159063_p256264_0038` campaign=`micro_1779517720159158_p256264_0042` reward=`0` new_paths=`0` promoted=`false`
