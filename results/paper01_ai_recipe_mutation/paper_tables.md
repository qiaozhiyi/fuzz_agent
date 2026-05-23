# Paper 01 Data Tables

Generated from real FuzzPilot artifacts. Median values are from the 5+5 completed E1a/E1b runs (`runs/p1_e1_cjson_*/`). `median_edges` is the `edges_found` field of each run's final `fuzzer_stats`; the aggregator script does not yet surface this column automatically — values below were filled in by hand after pulling `fuzzer_stats` from each run dir.

## Main Coverage Summary

| target | paper_mode | implementation_mode | repeats_observed | median_paths_total | median_edges | median_bitmap_cvg | unique_crashes_reported_sum | notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 0 | 269 | 23.21 | 0 | all 5/5 runs reach 269 edges (cJSON target ceiling) |
| cjson_parser | AI recipe validated | full-agent | 5 | 0 | 269 | 23.21 | 0 | 4/5 runs reach 269; r02 reaches 266 (bitmap_cvg 22.95%) |

## Recipe Quality Summary

| target | recipe_source | candidates | promoted | promotion_rate | median_reward | median_hit_rate | median_new_paths | median_new_edges | unique_crashes_reported_sum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AI | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

**Note on `promoted = 0`:** All 5 full-agent runs fire exactly one plateau cycle, each producing 4 micro-campaign candidates (total 20 across the 5 runs). Every micro-campaign returns `new_edges = 0, new_paths = 0, reward = 0` because the parent corpus at plateau time has already saturated cJSON's 269-edge reachable space (see `events.jsonl:micro_result` records for the raw numbers). The controller therefore emits `winner_decided:status=no_significance` followed by `promotion_skipped:reason=no_successful_micro_campaign`, and no LLM-proposed recipe enters the main `main_recipes/` store. The single recipe present in `main_recipes/` per run is the controller's hardcoded default `DictionaryAgent` recipe (tokens=["FUZZ","MAGIC","TOKEN"]), shared with rule-only runs.

## Throughput / Overhead Summary

| target | paper_mode | implementation_mode | repeats_observed | exec_sec_median | relative_exec_sec_delta_vs_baseline | median_recipe_hit_rate | notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cjson_parser | AFL++ default | baseline-afl | 5 | 13049 | 0 |  | from fuzzer_stats:execs_per_sec |
| cjson_parser | AI recipe validated | full-agent | 5 | 13816 | +5.88% |  | ratio 1.0587× (passes ≥0.85 gate) |

## Missing Paper Modes

| paper_mode | status | implementation_gap |
| --- | --- | --- |
| Random recipe | missing results | no first-class random-recipe ablation in current CLI |
| Rule recipe | missing results |  |
| AI recipe direct | missing results | no first-class direct-promotion ablation in current CLI |
