# Paper 01 Data Validity Report

- observed runs: `5`
- expected manifest runs: `21`
- missing expected runs: `16`
- extra observed runs: `0`
- missing required paper modes: `4`
- runs with artifact notes: `5`

## Missing Expected Runs

| target | implementation_mode | repeat |
| --- | --- | --- |
| cjson_parser | full-agent | 01 |
| cjson_parser | full-agent | 02 |
| cjson_parser | full-agent | 03 |
| cjson_parser | full-agent | 04 |
| cjson_parser | full-agent | 05 |
| cjson_parser | no-mutator | 01 |
| cjson_parser | no-mutator | 02 |
| cjson_parser | no-mutator | 03 |
| cjson_parser | no-static-analysis | 01 |
| cjson_parser | no-static-analysis | 02 |
| cjson_parser | no-static-analysis | 03 |
| cjson_parser | rule-only | 01 |
| cjson_parser | rule-only | 02 |
| cjson_parser | rule-only | 03 |
| libpng_parser | baseline-afl | 01 |
| libpng_parser | full-agent | 01 |

## Artifact Notes

| target | implementation_mode | repeat | run_id | run_status | notes |
| --- | --- | --- | --- | --- | --- |
| cjson_parser | baseline-afl | 01 | p1_e1_cjson_baseline-afl_r01 | failed_short_run | missing report.md |
| cjson_parser | baseline-afl | 02 | p1_e1_cjson_baseline-afl_r02 | failed_short_run | missing report.md |
| cjson_parser | baseline-afl | 03 | p1_e1_cjson_baseline-afl_r03 | failed_short_run | missing report.md |
| cjson_parser | baseline-afl | 04 | p1_e1_cjson_baseline-afl_r04 | failed_short_run | missing report.md |
| cjson_parser | baseline-afl | 05 | p1_e1_cjson_baseline-afl_r05 | failed_short_run | missing report.md |

## Missing Paper Modes

| paper_mode | status | implementation_gap |
| --- | --- | --- |
| Random recipe | missing results | no first-class random-recipe ablation in current CLI |
| Rule recipe | missing results |  |
| AI recipe direct | missing results | no first-class direct-promotion ablation in current CLI |
| AI recipe validated | missing results |  |

