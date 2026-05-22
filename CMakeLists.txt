cmake_minimum_required(VERSION 3.22)

project(FuzzPilot
  VERSION 0.1.0
  DESCRIPTION "Self-diagnosing AFL++ controller and recipe-driven custom mutator"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SQLite3 REQUIRED)

add_library(fuzzpilot_core STATIC
  src/string_util.cpp
  src/config.cpp
  src/controller/run.cpp
  src/env.cpp
  src/experiments/m6_matrix.cpp
  src/json_utils.cpp
  src/runner/afl_runner.cpp
  src/runner/process_posix.cpp
  src/model/gateway.cpp
  src/agents/agent_runtime.cpp
  src/telemetry/afl_stats.cpp
  src/telemetry/collector.cpp
  src/telemetry/mutation_events.cpp
  src/plateau/detector.cpp
  src/mutation/strategy.cpp
  src/mutation/recipe_store.cpp
  src/mutation/operator_sampler.cpp
  src/mutation/range_selector.cpp
  src/micro/manager.cpp
  src/micro/evaluator.cpp
  src/storage/db.cpp
  src/interventions/default.cpp
)

target_include_directories(fuzzpilot_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fuzzpilot_core PUBLIC SQLite::SQLite3)

if(APPLE)
  target_compile_definitions(fuzzpilot_core PUBLIC FUZZPILOT_PLATFORM_MACOS=1)
elseif(UNIX)
  target_compile_definitions(fuzzpilot_core PUBLIC FUZZPILOT_PLATFORM_UNIX=1)
endif()

target_compile_options(fuzzpilot_core PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(fuzzpilot src/cli/main.cpp)
target_link_libraries(fuzzpilot PRIVATE fuzzpilot_core)

add_subdirectory(mutators/fuzzpilot)
add_subdirectory(tools/mutator_microbench)

add_executable(fuzzpilot_mutator_smoke
  tools/mutator_smoke.cpp
  mutators/fuzzpilot/mutator_core.cpp
  mutators/fuzzpilot/recipe_cache.cpp
  mutators/fuzzpilot/telemetry_ring.cpp
)
target_include_directories(fuzzpilot_mutator_smoke PRIVATE mutators/fuzzpilot)
target_compile_features(fuzzpilot_mutator_smoke PRIVATE cxx_std_20)

add_executable(fuzzpilot_process_capture_smoke tools/process_capture_smoke.cpp)
target_link_libraries(fuzzpilot_process_capture_smoke PRIVATE fuzzpilot_core)

add_executable(fuzzpilot_json_utils_smoke tools/json_utils_smoke.cpp)
target_link_libraries(fuzzpilot_json_utils_smoke PRIVATE fuzzpilot_core)

enable_testing()
add_test(NAME fuzzpilot_version COMMAND fuzzpilot --version)
add_test(NAME fuzzpilot_process_capture_smoke COMMAND fuzzpilot_process_capture_smoke)
add_test(NAME fuzzpilot_json_utils_smoke COMMAND fuzzpilot_json_utils_smoke)
add_test(NAME fuzzpilot_check_example
  COMMAND fuzzpilot check-config --config ${CMAKE_CURRENT_SOURCE_DIR}/configs/examples/libpng.yaml)
add_test(NAME fuzzpilot_parse_stats
  COMMAND fuzzpilot parse-stats --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_newer)
add_test(NAME fuzzpilot_detect_plateau
  COMMAND fuzzpilot detect-plateau
    --older ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_older
    --newer ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_newer
    --window-sec 600)
add_test(NAME fuzzpilot_init_db
  COMMAND fuzzpilot init-db
    --db ${CMAKE_CURRENT_BINARY_DIR}/smoke/fuzzpilot.sqlite
    --schema ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql)
add_test(NAME fuzzpilot_write_recipe
  COMMAND fuzzpilot write-default-recipe
    --recipe-store ${CMAKE_CURRENT_BINARY_DIR}/smoke/recipes)
add_test(NAME fuzzpilot_parse_mutator_telemetry
  COMMAND fuzzpilot parse-mutator-telemetry
    --mutator-telemetry ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mutator_telemetry.jsonl)
add_test(NAME fuzzpilot_replay_telemetry
  COMMAND fuzzpilot replay-telemetry
    --db ${CMAKE_CURRENT_BINARY_DIR}/smoke/m1.sqlite
    --schema ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql
    --run-id run_m1_smoke
    --campaign-id main_m1_smoke
    --window-sec 600
    --jsonl ${CMAKE_CURRENT_BINARY_DIR}/smoke/m1_events.jsonl
    --coverage-csv ${CMAKE_CURRENT_BINARY_DIR}/smoke/m1_coverage.csv
    --mutator-telemetry ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mutator_telemetry.jsonl
    --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_older
    --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_newer)
add_test(NAME fuzzpilot_write_m2_recipes
  COMMAND fuzzpilot write-m2-recipes
    --recipe-store ${CMAKE_CURRENT_BINARY_DIR}/smoke/m2_recipes
    --seed-id id:000123
    --seed-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed_m2.bin
    --token MAGIC
    --token TOKEN)
set_tests_properties(fuzzpilot_write_m2_recipes PROPERTIES FIXTURES_SETUP m2_recipes)
add_test(NAME fuzzpilot_lookup_m2_seed_id
  COMMAND fuzzpilot lookup-recipe
    --recipe-store ${CMAKE_CURRENT_BINARY_DIR}/smoke/m2_recipes
    --seed-id id:000123,src:000001,time:0)
set_tests_properties(fuzzpilot_lookup_m2_seed_id PROPERTIES FIXTURES_REQUIRED m2_recipes)
add_test(NAME fuzzpilot_lookup_m2_seed_hash
  COMMAND fuzzpilot lookup-recipe
    --recipe-store ${CMAKE_CURRENT_BINARY_DIR}/smoke/m2_recipes
    --seed-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed_m2.bin)
set_tests_properties(fuzzpilot_lookup_m2_seed_hash PROPERTIES FIXTURES_REQUIRED m2_recipes)
add_test(NAME fuzzpilot_mutator_m2_lookup_smoke
  COMMAND fuzzpilot_mutator_smoke
    --recipe-store ${CMAKE_CURRENT_BINARY_DIR}/smoke/m2_recipes
    --input-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed_m2.bin
    --seed-name id:000123,src:000001,time:0
    --telemetry ${CMAKE_CURRENT_BINARY_DIR}/smoke/m2_mutator_telemetry.jsonl)
set_tests_properties(fuzzpilot_mutator_m2_lookup_smoke PROPERTIES FIXTURES_REQUIRED m2_recipes)
add_test(NAME fuzzpilot_m3_snapshot_corpus
  COMMAND fuzzpilot snapshot-corpus
    --afl-output-dir ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/afl_out
    --snapshot-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/m3_snapshot)
set_tests_properties(fuzzpilot_m3_snapshot_corpus PROPERTIES FIXTURES_SETUP m3_snapshot)
add_test(NAME fuzzpilot_m3_plan_micro
  COMMAND fuzzpilot plan-micro-campaigns
    --config ${CMAKE_CURRENT_SOURCE_DIR}/configs/examples/libpng.yaml
    --snapshot-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/m3_snapshot
    --work-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/m3_micro
    --plateau-id plateau_m3_smoke)
set_tests_properties(fuzzpilot_m3_plan_micro PROPERTIES FIXTURES_REQUIRED m3_snapshot)
add_test(NAME fuzzpilot_m3_evaluate_micro
  COMMAND fuzzpilot evaluate-micro
    --parent-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_older
    --micro-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_newer
    --intervention-id intv_smoke
    --campaign-id micro_smoke
    --db ${CMAKE_CURRENT_BINARY_DIR}/smoke/m3_eval.sqlite
    --schema ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql)
add_test(NAME fuzzpilot_m4_run_model_agents
  COMMAND fuzzpilot run-model-agents
    --db ${CMAKE_CURRENT_BINARY_DIR}/smoke/m4_agents.sqlite
    --schema ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql
    --run-id run_m4_smoke
    --plateau-id plateau_m4_smoke
    --blackboard-json "{\"plateau\":{\"reason\":\"no_new_paths\"}}")
add_test(NAME fuzzpilot_m6_matrix
  COMMAND fuzzpilot m6-matrix
    --config ${CMAKE_CURRENT_SOURCE_DIR}/experiments/targets/cjson/config.yaml
    --config ${CMAKE_CURRENT_SOURCE_DIR}/experiments/targets/libpng/config.yaml
    --out-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/m6_matrix
    --work-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/m6_work
    --repeats 1
    --main-budget-sec 3600
    --micro-budget-sec 120)
add_test(NAME fuzzpilot_mvp_run_dry
  COMMAND fuzzpilot run
    --config ${CMAKE_CURRENT_SOURCE_DIR}/configs/examples/libpng.yaml
    --work-dir ${CMAKE_CURRENT_BINARY_DIR}/smoke/mvp_run
    --schema ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql
    --afl-output-dir ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/afl_out
    --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_older
    --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_newer
    --micro-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_micro_control
    --micro-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_micro_dictionary
    --micro-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_micro_seed_focus
    --micro-stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fuzzer_stats_micro_winner
    --provider fake)
