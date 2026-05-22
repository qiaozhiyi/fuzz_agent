# Paper 01 Data Tables

Generated from real FuzzPilot artifacts only. Empty cells mean the current artifact schema does not expose that metric yet.

## Main Coverage Summary

| target | paper_mode | implementation_mode | repeats_observed | median_paths_total | median_edges | median_bitmap_cvg | unique_crashes_reported_sum | notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 0 |  | 21.84 | 0 | edges not present in current telemetry; use bitmap_cvg until edge export exists |

## Recipe Quality Summary

| target | recipe_source | candidates | promoted | promotion_rate | median_reward | median_hit_rate | median_new_paths | median_new_edges | unique_crashes_reported_sum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |  |

## Throughput / Overhead Summary

| target | paper_mode | implementation_mode | repeats_observed | exec_sec_median | relative_exec_sec_delta_vs_baseline | median_recipe_hit_rate | notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 150327 | 0 |  | relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims |

## Missing Paper Modes

| paper_mode | status | implementation_gap |
| --- | --- | --- |
| Random recipe | missing results | no first-class random-recipe ablation in current CLI |
| Rule recipe | missing results |  |
| AI recipe direct | missing results | no first-class direct-promotion ablation in current CLI |
| AI recipe validated | missing results |  |

