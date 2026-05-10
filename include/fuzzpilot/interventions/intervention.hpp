#pragma once

#include <map>
#include <string>
#include <vector>

namespace fuzzpilot {

struct Intervention {
  std::string id;
  std::string agent;
  std::string hypothesis;
  std::string action;
  std::map<std::string, std::string> params;
  std::string expected_signal = "new_edges";
  std::string risk = "low";
  bool reproducible = true;
};

std::vector<Intervention> default_v0_interventions(int budget_sec);
std::string intervention_json(const Intervention& intervention);

}  // namespace fuzzpilot

