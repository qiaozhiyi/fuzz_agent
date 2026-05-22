# tools/mutator_microbench/CMakeLists.txt
#
# Standalone microbenchmark used by Paper 1 Experiment E3.
# Builds:
#   - mutator_microbench: driver that dlopens a mutator .so and times calls.
#   - libvanilla_havoc.so: a dispatch-symmetric AFL++ havoc-equivalent
#     baseline (same C ABI as the FuzzPilot mutator), so the vanilla vs
#     fp-* comparison measures internal mutator cost, not in-process vs
#     dlopen asymmetry.

add_executable(mutator_microbench main.cpp)
target_link_libraries(mutator_microbench PRIVATE ${CMAKE_DL_LIBS})
target_compile_features(mutator_microbench PRIVATE cxx_std_17)
set_target_properties(mutator_microbench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/mutator_microbench"
)

add_library(vanilla_havoc SHARED vanilla_havoc.cpp)
target_compile_features(vanilla_havoc PRIVATE cxx_std_17)
set_target_properties(vanilla_havoc PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/mutator_microbench"
  PREFIX "lib"
  POSITION_INDEPENDENT_CODE ON
)

install(TARGETS mutator_microbench
  RUNTIME DESTINATION bin
)
install(TARGETS vanilla_havoc
  LIBRARY DESTINATION lib
)
