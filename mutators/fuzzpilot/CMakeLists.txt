add_library(fuzzpilot_mutator MODULE
  afl_custom_mutator.c
  mutator_core.cpp
  recipe_cache.cpp
  telemetry_ring.cpp
)

target_include_directories(fuzzpilot_mutator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fuzzpilot_mutator PRIVATE cxx_std_20)
set_target_properties(fuzzpilot_mutator PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME "fuzzpilot_mutator"
  POSITION_INDEPENDENT_CODE ON
)

if(APPLE)
  set_target_properties(fuzzpilot_mutator PROPERTIES SUFFIX ".dylib")
endif()

target_compile_options(fuzzpilot_mutator PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

