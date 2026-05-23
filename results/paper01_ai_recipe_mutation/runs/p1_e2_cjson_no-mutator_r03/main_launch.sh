#!/usr/bin/env sh
AFL_MAP_SIZE=65536 AFL_NO_UI=1 AFL_SKIP_CPUFREQ=1 afl-fuzz -i experiments/targets/cjson/seeds -o /root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e2_cjson_no-mutator_r03/work/run_1779517826980444_p318617_0000/main_out -m 1024 -t 1000 -- experiments/targets/cjson/cjson_fuzzer @@
