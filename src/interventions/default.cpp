#include "fuzzpilot/interventions/intervention.hpp"

#include "fuzzpilot/ids.hpp"

#include <sstream>

namespace fuzzpilot {

std::vector<Intervention> default_v0_interventions(int budget_sec) {
  return {
      {
          .id = make_id("intv_default"),
          .agent = "CoordinatorAgent",
          .hypothesis = "control campaign for plateau validation",
          .action = "default_control",
          .params = {{"budget_sec", std::to_string(budget_sec)}},
          .tokens = {},
          .source_decision_id = "",
          .params_json = "{}",
          .spiral_stage = 0,
          .priority = 1.0,
          .depends_on_intervention_id = "",
          .expected_signal = "new_edges",
          .risk = "low",
          .reproducible = true,
      },
      {
          .id = make_id("intv_dictionary"),
          .agent = "DictionaryAgent",
          .hypothesis = "plateau may be caused by missing magic bytes or parser keywords",
          .action = "dictionary_probe",
          .params = {{"budget_sec", std::to_string(budget_sec)}},
          .tokens = {},
          .source_decision_id = "",
          .params_json = "{}",
          .spiral_stage = 1,
          .priority = 0.9,
          .depends_on_intervention_id = "",
          .expected_signal = "new_edges",
          .risk = "low",
          .reproducible = true,
      },
      {
          .id = make_id("intv_seed_focus"),
          .agent = "SchedulerAgent",
          .hypothesis = "coverage may improve by focusing energy on recent or favored seeds",
          .action = "seed_focus_probe",
          .params = {{"budget_sec", std::to_string(budget_sec)}},
          .tokens = {},
          .source_decision_id = "",
          .params_json = "{}",
          .spiral_stage = 2,
          .priority = 0.8,
          .depends_on_intervention_id = "",
          .expected_signal = "new_edges",
          .risk = "medium",
          .reproducible = true,
      },
      {
          .id = make_id("intv_per_seed_recipe"),
          .agent = "MutatorAgent",
          .hypothesis = "model-proposed per-seed mutation recipes may unlock narrow parser states",
          .action = "per_seed_recipe_probe",
          .params = {{"budget_sec", std::to_string(budget_sec)}},
          .tokens = {},
          .source_decision_id = "",
          .params_json = "{}",
          .spiral_stage = 3,
          .priority = 0.7,
          .depends_on_intervention_id = "",
          .expected_signal = "new_edges",
          .risk = "medium",
          .reproducible = true,
      },
  };
}

std::string intervention_json(const Intervention& intervention) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << intervention.id << "\",";
  out << "\"agent\":\"" << intervention.agent << "\",";
  out << "\"hypothesis\":\"" << intervention.hypothesis << "\",";
  out << "\"action\":\"" << intervention.action << "\",";
  out << "\"params\":{";
  bool first = true;
  for (const auto& [key, value] : intervention.params) {
    if (!first) out << ",";
    first = false;
    out << "\"" << key << "\":\"" << value << "\"";
  }
  out << "},";
  out << "\"expected_signal\":\"" << intervention.expected_signal << "\",";
  out << "\"spiral_stage\":" << intervention.spiral_stage << ",";
  out << "\"priority\":" << intervention.priority << ",";
  out << "\"depends_on_intervention_id\":\"" << intervention.depends_on_intervention_id << "\",";
  out << "\"risk\":\"" << intervention.risk << "\",";
  out << "\"reproducible\":" << (intervention.reproducible ? "true" : "false");
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
