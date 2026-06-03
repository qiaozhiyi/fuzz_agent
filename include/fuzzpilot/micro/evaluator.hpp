#pragma once

#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace fuzzpilot {

struct MicroCampaignSpec;

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

struct MicroWinnerSelection {
  bool has_valid_results = false;
  bool selected = false;
  std::size_t result_index = 0;
  uint64_t valid_results = 0;
  double control_reward = 0.0;
  double winner_reward = 0.0;
  double improvement_over_control = 0.0;
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

MicroWinnerSelection select_micro_winner_against_control(
    const std::vector<MicroResult>& results,
    const std::vector<MicroCampaignSpec>& specs,
    const std::set<std::string>& failed_campaign_ids,
    double abs_margin = 0.1,
    double rel_margin = 0.05);

bool should_persist_micro_result(const MicroResult& result,
                                 const std::set<std::string>& failed_campaign_ids);

std::string micro_result_json(const MicroResult& result);

}  // namespace fuzzpilot
