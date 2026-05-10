#pragma once

#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <cstdint>
#include <string>

namespace fuzzpilot {

struct MicroResult {
  std::string id;
  std::string intervention_id;
  std::string campaign_id;
  uint64_t execs_done = 0;
  uint64_t new_paths = 0;
  uint64_t new_edges = 0;
  uint64_t unique_crashes = 0;
  uint64_t recipe_hits = 0;
  uint64_t recipe_misses = 0;
  double reward = 0.0;
  bool promoted = false;
};

MicroResult evaluate_micro_result(const std::string& intervention_id,
                                  const std::string& campaign_id,
                                  const AflStats& parent,
                                  const AflStats& micro);

std::string micro_result_json(const MicroResult& result);

}  // namespace fuzzpilot

