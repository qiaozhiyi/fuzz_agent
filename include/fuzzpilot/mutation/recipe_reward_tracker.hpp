#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fuzzpilot {

// One past LLM-emitted decision plus the edges-growth credit attributed to it
// over a fixed wall-clock window after deployment. The tracker is the source
// of "in-context RL" few-shot examples handed back to the next agent prompt.
//
// Reward attribution is intentionally coarse: at each AFL telemetry tick we
// take the delta of edges_found and distribute it equally across all
// decisions deployed in the past credit_window_sec. We don't try to attribute
// per-recipe inside the mutator (would need shm changes). For prompt-side
// few-shot ranking this resolution is sufficient.
struct RewardExample {
  std::string decision_id;
  std::string agent_name;
  std::string summary;       // short human/LLM-readable proposal summary
  uint64_t deploy_ts = 0;    // wall-clock seconds, == ts at record_deploy
  double reward = 0.0;       // accumulated edge-growth credit
  uint64_t apply_count = 0;  // count of process_stats ticks that credited it
};

class RecipeRewardTracker {
 public:
  RecipeRewardTracker(std::filesystem::path persist_path,
                      uint64_t credit_window_sec = 600);

  // Record an LLM decision deployment. summary should be a short string
  // suitable for embedding into a future prompt.
  void record_deploy(const std::string& decision_id,
                     const std::string& agent_name,
                     const std::string& summary,
                     uint64_t ts);

  // Observe absolute edges_found at ts. Internally diffs against the last
  // observation and distributes the positive delta across all decisions
  // deployed within [ts - credit_window_sec, ts]. First call only seeds.
  void observe_edges(uint64_t edges_found, uint64_t ts);

  // Return the k highest-reward / lowest-reward decisions. Stable order by
  // (reward desc, deploy_ts desc) for top; (reward asc, deploy_ts desc) for
  // bottom. Only decisions with apply_count > 0 are eligible for bottom.
  std::vector<RewardExample> topk(std::size_t k) const;
  std::vector<RewardExample> bottomk(std::size_t k) const;

  std::size_t size() const;

 private:
  void persist_locked(const RewardExample& ex);

  mutable std::mutex mutex_;
  std::filesystem::path persist_path_;
  uint64_t credit_window_sec_;
  bool have_baseline_ = false;
  uint64_t last_edges_ = 0;
  std::deque<RewardExample> decisions_;
  std::map<std::string, std::size_t> by_id_;
};

}  // namespace fuzzpilot
