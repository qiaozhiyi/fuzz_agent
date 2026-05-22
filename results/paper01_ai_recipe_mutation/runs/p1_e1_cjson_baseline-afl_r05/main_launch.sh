#!/usr/bin/env sh
AFL_MAP_SIZE=65536 AFL_NO_UI=1 AFL_SKIP_CPUFREQ=1 afl-fuzz -i experiments/targets/cjson/seeds -o /work/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e1_cjson_baseline-afl_r05/work/run_1779372554600364_p7583_0000/main_out -m 1024 -t 1000 -- experiments/targets/cjson/cjson_fuzzer @@
