#include "fuzzpilot/mutation/recipe_reward_tracker.hpp"

#include "fuzzpilot/json_utils.hpp"

#include <algorithm>
#include <fstream>

namespace fuzzpilot {

RecipeRewardTracker::RecipeRewardTracker(std::filesystem::path persist_path,
                                         uint64_t credit_window_sec)
    : persist_path_(std::move(persist_path)),
      credit_window_sec_(credit_window_sec) {}

void RecipeRewardTracker::record_deploy(const std::string& decision_id,
                                        const std::string& agent_name,
                                        const std::string& summary,
                                        uint64_t ts) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (by_id_.find(decision_id) != by_id_.end()) {
    return;
  }
  RewardExample ex;
  ex.decision_id = decision_id;
  ex.agent_name = agent_name;
  ex.summary = summary;
  ex.deploy_ts = ts;
  ex.reward = 0.0;
  ex.apply_count = 0;
  by_id_[decision_id] = decisions_.size();
  decisions_.push_back(std::move(ex));
  persist_locked(decisions_.back());
}

void RecipeRewardTracker::observe_edges(uint64_t edges_found, uint64_t ts) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!have_baseline_) {
    have_baseline_ = true;
    last_edges_ = edges_found;
    return;
  }
  // Counter resets / corpus replays shouldn't credit anyone.
  if (edges_found <= last_edges_) {
    last_edges_ = edges_found;
    return;
  }
  const uint64_t delta = edges_found - last_edges_;
  last_edges_ = edges_found;

  // Pick decisions within the credit window — walk from the back and
  // stop as soon as we cross the window boundary.
  std::vector<std::size_t> eligible;
  const uint64_t lower = ts > credit_window_sec_ ? ts - credit_window_sec_ : 0;
  for (std::size_t i = decisions_.size(); i-- > 0;) {
    if (decisions_[i].deploy_ts < lower) {
      break;
    }
    eligible.push_back(i);
  }
  if (eligible.empty()) {
    return;
  }
  const double share = static_cast<double>(delta) /
                       static_cast<double>(eligible.size());
  for (std::size_t idx : eligible) {
    auto& ex = decisions_[idx];
    ex.reward += share;
    ex.apply_count += 1;
    persist_locked(ex);
  }
}

std::vector<RewardExample> RecipeRewardTracker::topk(std::size_t k) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<RewardExample> snapshot(decisions_.begin(), decisions_.end());
  std::sort(snapshot.begin(), snapshot.end(),
            [](const RewardExample& a, const RewardExample& b) {
              if (a.reward != b.reward) return a.reward > b.reward;
              return a.deploy_ts > b.deploy_ts;
            });
  if (snapshot.size() > k) snapshot.resize(k);
  return snapshot;
}

std::vector<RewardExample> RecipeRewardTracker::bottomk(std::size_t k) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<RewardExample> snapshot;
  snapshot.reserve(decisions_.size());
  for (const auto& ex : decisions_) {
    if (ex.apply_count > 0) snapshot.push_back(ex);
  }
  std::sort(snapshot.begin(), snapshot.end(),
            [](const RewardExample& a, const RewardExample& b) {
              if (a.reward != b.reward) return a.reward < b.reward;
              return a.deploy_ts > b.deploy_ts;
            });
  if (snapshot.size() > k) snapshot.resize(k);
  return snapshot;
}

std::size_t RecipeRewardTracker::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return decisions_.size();
}

void RecipeRewardTracker::persist_locked(const RewardExample& ex) {
  if (persist_path_.empty()) return;
  std::ofstream out(persist_path_, std::ios::app);
  if (!out) return;
  out << "{\"decision_id\":\"" << json_escape(ex.decision_id)
      << "\",\"agent\":\"" << json_escape(ex.agent_name)
      << "\",\"summary\":\"" << json_escape(ex.summary)
      << "\",\"deploy_ts\":" << ex.deploy_ts
      << ",\"reward\":" << ex.reward
      << ",\"apply_count\":" << ex.apply_count
      << "}\n";
}

}  // namespace fuzzpilot
