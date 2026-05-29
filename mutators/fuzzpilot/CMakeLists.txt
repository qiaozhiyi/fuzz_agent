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

if(WIN32)
  add_custom_command(TARGET fuzzpilot_mutator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:fuzzpilot_mutator>"
      "$<TARGET_FILE_DIR:fuzzpilot_mutator>/libfuzzpilot_mutator"
    COMMENT "Creating extensionless fuzzpilot mutator copy")
else()
  add_custom_command(TARGET fuzzpilot_mutator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f
      "$<TARGET_FILE_DIR:fuzzpilot_mutator>/libfuzzpilot_mutator"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
      "$<TARGET_FILE_NAME:fuzzpilot_mutator>"
      "$<TARGET_FILE_DIR:fuzzpilot_mutator>/libfuzzpilot_mutator"
    COMMENT "Creating extensionless fuzzpilot mutator link")
endif()

target_compile_options(fuzzpilot_mutator PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
  $<$<CONFIG:Release>:-O3 -DNDEBUG>
  $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
)
