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
  // Decomposed components — kept separately so downstream analysis can
  // re-weight or run Pareto comparison without losing information.
  double edge_score = 0.0;
  double path_score = 0.0;
  double crash_score = 0.0;
  double recipe_score = 0.0;
  double reward = 0.0;
  // True if parent had no edges_found telemetry (AFL++ <3.x or missing field);
  // in that case edge_score falls back to path_score and the run should be
  // flagged in analysis.
  bool edges_unavailable = false;
  bool promoted = false;
};

// Reward computation policy. Production default is `kEdgeWeighted`, but the
// CLI / experiment matrix can override to `kEdgesOnly` or `kRandom` for
// ablation studies.
enum class RewardMode {
  kEdgeWeighted,  // default: edges*1 + crashes*10, paths only when edges missing
  kEdgesOnly,     // ablation: reward = new_edges (paths ignored)
  kPathsOnly,     // legacy parity with pre-fix behaviour
  kRandom,        // ablation: uniform random in [0,1) for sanity check
};

MicroResult evaluate_micro_result(const std::string& intervention_id,
                                  const std::string& campaign_id,
                                  const AflStats& parent,
                                  const AflStats& micro,
                                  RewardMode mode = RewardMode::kEdgeWeighted);

std::string micro_result_json(const MicroResult& result);

}  // namespace fuzzpilot

