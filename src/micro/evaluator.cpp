#include "fuzzpilot/micro/evaluator.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/micro/manager.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <sstream>

namespace fuzzpilot {
namespace {

uint64_t saturating_delta(uint64_t after, uint64_t before) {
  return after > before ? after - before : 0;
}

// Reward weights. Edges are the primary signal because they correspond
// to AFL++'s coverage bitmap; crashes carry a small bonus but never
// dominate (a single crash is worth ~10 edges, not 10^4 as before).
constexpr double kEdgeWeight = 1.0;
constexpr double kPathWeightFallback = 0.5;  // only used when edges missing
constexpr double kCrashBonus = 10.0;
constexpr double kRecipeHitBonus = 0.001;
constexpr double kRecipeMissPenalty = 0.0005;

double random_reward(const std::string& campaign_id) {
  // Deterministic per-campaign so a single replay is reproducible.
  std::seed_seq seed(campaign_id.begin(), campaign_id.end());
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(rng);
}

}  // namespace

MicroResult evaluate_micro_result(const std::string& intervention_id,
                                  const std::string& campaign_id,
                                  const AflStats& parent,
                                  const AflStats& micro,
                                  RewardMode mode) {
  MicroResult result;
  result.id = make_id("micro_result");
  result.intervention_id = intervention_id;
  result.campaign_id = campaign_id;
  result.execs_done = micro.execs_done;
  result.new_paths = saturating_delta(micro.paths_total, parent.paths_total);
  result.new_edges = saturating_delta(micro.edges_found, parent.edges_found);
  result.unique_crashes = saturating_delta(micro.unique_crashes, parent.unique_crashes);
  result.recipe_hits = micro.recipe_hits;
  result.recipe_misses = micro.recipe_misses;
  result.edges_unavailable = (parent.edges_found == 0 && micro.edges_found == 0);

  // Decompose so analysis can reweight without re-running anything.
  result.edge_score = static_cast<double>(result.new_edges) * kEdgeWeight;
  result.path_score = static_cast<double>(result.new_paths) * kPathWeightFallback;
  result.crash_score = static_cast<double>(result.unique_crashes) * kCrashBonus;
  result.recipe_score = static_cast<double>(result.recipe_hits) * kRecipeHitBonus -
                        static_cast<double>(result.recipe_misses) * kRecipeMissPenalty;

  switch (mode) {
    case RewardMode::kEdgesOnly:
      result.reward = result.edge_score;
      break;
    case RewardMode::kPathsOnly:
      result.reward = result.path_score / kPathWeightFallback;  // raw paths
      break;
    case RewardMode::kRandom:
      result.reward = random_reward(campaign_id);
      break;
    case RewardMode::kEdgeWeighted:
    default: {
      const double coverage_score = result.edges_unavailable
                                        ? result.path_score
                                        : result.edge_score + result.path_score * 0.1;
      result.reward = coverage_score + result.crash_score + result.recipe_score;
      break;
    }
  }
  return result;
}

MicroWinnerSelection select_micro_winner_against_control(
    const std::vector<MicroResult>& results,
    const std::vector<MicroCampaignSpec>& specs,
    const std::set<std::string>& failed_campaign_ids,
    double abs_margin,
    double rel_margin) {
  MicroWinnerSelection selection;
  if (results.empty()) {
    return selection;
  }

  std::map<std::string, std::string> action_by_campaign;
  for (const auto& spec : specs) {
    action_by_campaign[spec.id] = spec.name;
  }

  bool have_control = false;
  for (const auto& result : results) {
    if (failed_campaign_ids.count(result.campaign_id) > 0) {
      continue;
    }
    ++selection.valid_results;
    selection.has_valid_results = true;
    const auto action_it = action_by_campaign.find(result.campaign_id);
    if (action_it != action_by_campaign.end() &&
        action_it->second == "default_control") {
      if (!have_control || result.reward > selection.control_reward) {
        selection.control_reward = result.reward;
        have_control = true;
      }
    }
  }

  if (!selection.has_valid_results) {
    return selection;
  }

  bool have_winner = false;
  double second_best_reward = have_control ? selection.control_reward : 0.0;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    if (failed_campaign_ids.count(result.campaign_id) > 0) {
      continue;
    }
    const auto action_it = action_by_campaign.find(result.campaign_id);
    const bool is_control = action_it != action_by_campaign.end() &&
                            action_it->second == "default_control";
    if (is_control) {
      continue;
    }
    if (!have_winner || result.reward > selection.winner_reward) {
      if (have_winner) {
        second_best_reward = std::max(second_best_reward, selection.winner_reward);
      }
      selection.result_index = i;
      selection.winner_reward = result.reward;
      have_winner = true;
    } else {
      second_best_reward = std::max(second_best_reward, result.reward);
    }
  }

  if (!have_winner) {
    return selection;
  }

  selection.improvement_over_control =
      selection.winner_reward - selection.control_reward;
  const double control_margin =
      std::max(abs_margin, std::abs(selection.control_reward) * rel_margin);
  const double peer_margin =
      std::max(abs_margin, std::abs(selection.winner_reward) * rel_margin);
  selection.selected =
      selection.improvement_over_control > control_margin &&
      (selection.winner_reward - second_best_reward) > peer_margin;
  return selection;
}

bool should_persist_micro_result(const MicroResult& result,
                                 const std::set<std::string>& failed_campaign_ids) {
  return failed_campaign_ids.count(result.campaign_id) == 0;
}

std::string micro_result_json(const MicroResult& result) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << result.id << "\",";
  out << "\"intervention_id\":\"" << result.intervention_id << "\",";
  out << "\"campaign_id\":\"" << result.campaign_id << "\",";
  out << "\"execs_done\":" << result.execs_done << ",";
  out << "\"new_paths\":" << result.new_paths << ",";
  out << "\"new_edges\":" << result.new_edges << ",";
  out << "\"unique_crashes\":" << result.unique_crashes << ",";
  out << "\"recipe_hits\":" << result.recipe_hits << ",";
  out << "\"recipe_misses\":" << result.recipe_misses << ",";
  out << "\"edge_score\":" << result.edge_score << ",";
  out << "\"path_score\":" << result.path_score << ",";
  out << "\"crash_score\":" << result.crash_score << ",";
  out << "\"recipe_score\":" << result.recipe_score << ",";
  out << "\"reward\":" << result.reward << ",";
  out << "\"edges_unavailable\":" << (result.edges_unavailable ? "true" : "false") << ",";
  out << "\"promoted\":" << (result.promoted ? "true" : "false");
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
