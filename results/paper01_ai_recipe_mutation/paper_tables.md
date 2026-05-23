# Paper 01 Data Tables

Generated from real FuzzPilot artifacts only. Empty cells mean the current artifact schema does not expose that metric yet.

## Main Coverage Summary

| target | paper_mode | implementation_mode | repeats_observed | median_paths_total | median_edges | median_bitmap_cvg | unique_crashes_reported_sum | notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 0 |  | 23.21 | 0 | edges not present in current telemetry; use bitmap_cvg until edge export exists |
| cjson_parser | AI recipe validated | full-agent | 5 | 0 |  | 23.21 | 0 | edges not present in current telemetry; use bitmap_cvg until edge export exists |
| cjson_parser | Mutator disabled ablation | no-mutator | 3 | 0 |  | 23.21 | 0 | edges not present in current telemetry; use bitmap_cvg until edge export exists |
| cjson_parser | Rule recipe | rule-only | 3 | 0 |  | 23.21 | 0 | edges not present in current telemetry; use bitmap_cvg until edge export exists |

## Recipe Quality Summary

| target | recipe_source | candidates | promoted | promotion_rate | median_reward | median_hit_rate | median_new_paths | median_new_edges | unique_crashes_reported_sum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AI | 20 | 0 | 0 | 0 |  | 0 | 0 | 0 |
| cjson_parser | rule | 0 | 0 |  |  |  |  |  | 0 |

## Throughput / Overhead Summary

| target | paper_mode | implementation_mode | repeats_observed | exec_sec_median | relative_exec_sec_delta_vs_baseline | median_recipe_hit_rate | notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 13055.6 | 0 |  | relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims |
| cjson_parser | AI recipe validated | full-agent | 5 | 13812.2 | 0.0579522 |  | relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims |
| cjson_parser | Mutator disabled ablation | no-mutator | 3 | 12712.2 | -0.0263014 |  | relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims |
| cjson_parser | Rule recipe | rule-only | 3 | 13516.4 | 0.0352945 |  | relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims |

## Missing Paper Modes

| paper_mode | status | implementation_gap |
| --- | --- | --- |
| Random recipe | missing results | no first-class random-recipe ablation in current CLI |
| AI recipe direct | missing results | no first-class direct-promotion ablation in current CLI |

