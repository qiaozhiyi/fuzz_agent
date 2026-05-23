# Paper 01 Data Validity Report

- observed runs: `16`
- expected manifest runs: `21`
- missing expected runs: `5`
- extra observed runs: `0`
- missing required paper modes: `2`
- runs with artifact notes: `4`

## Missing Expected Runs

| target | implementation_mode | repeat |
| --- | --- | --- |
| cjson_parser | no-static-analysis | 01 |
| cjson_parser | no-static-analysis | 02 |
| cjson_parser | no-static-analysis | 03 |
| libpng_parser | baseline-afl | 01 |
| libpng_parser | full-agent | 01 |

## Artifact Notes

| target | implementation_mode | repeat | run_id | run_status | notes |
| --- | --- | --- | --- | --- | --- |
| cjson_parser | no-mutator | 03 | p1_e2_cjson_no-mutator_r03 | completed | missing fuzzpilot.sqlite |
| cjson_parser | rule-only | 01 | p1_e2_cjson_rule-only_r01 | completed | missing fuzzpilot.sqlite |
| cjson_parser | rule-only | 02 | p1_e2_cjson_rule-only_r02 | completed | missing fuzzpilot.sqlite |
| cjson_parser | rule-only | 03 | p1_e2_cjson_rule-only_r03 | completed | missing fuzzpilot.sqlite |

## Missing Paper Modes

| paper_mode | status | implementation_gap |
| --- | --- | --- |
| Random recipe | missing results | no first-class random-recipe ablation in current CLI |
| AI recipe direct | missing results | no first-class direct-promotion ablation in current CLI |

