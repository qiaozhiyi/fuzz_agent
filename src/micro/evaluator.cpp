#include "fuzzpilot/micro/evaluator.hpp"

#include "fuzzpilot/ids.hpp"

#include <sstream>

namespace fuzzpilot {

MicroResult evaluate_micro_result(const std::string& intervention_id,
                                  const std::string& campaign_id,
                                  const AflStats& parent,
                                  const AflStats& micro) {
  MicroResult result;
  result.id = make_id("micro_result");
  result.intervention_id = intervention_id;
  result.campaign_id = campaign_id;
  result.execs_done = micro.execs_done;
  result.new_paths = micro.paths_total > parent.paths_total ? micro.paths_total - parent.paths_total : 0;
  result.new_edges = result.new_paths;
  result.unique_crashes = micro.unique_crashes > parent.unique_crashes
                              ? micro.unique_crashes - parent.unique_crashes
                              : 0;
  result.recipe_hits = micro.recipe_hits;
  result.recipe_misses = micro.recipe_misses;
  result.reward = static_cast<double>(result.new_edges) * 10.0 +
                  static_cast<double>(result.new_paths) * 3.0 +
                  static_cast<double>(result.unique_crashes) * 100.0 +
                  static_cast<double>(result.recipe_hits) * 0.001 -
                  static_cast<double>(result.recipe_misses) * 0.002;
  return result;
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
  out << "\"reward\":" << result.reward << ",";
  out << "\"promoted\":" << (result.promoted ? "true" : "false");
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot

