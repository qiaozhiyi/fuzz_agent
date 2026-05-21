#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/json_utils.hpp"

#include <iostream>
#include <string>

int main() {
  const std::string valid =
      "{\"agent\":\"SmokeAgent\",\"interventions\":[],\"seed_strategies\":[]}";
  const std::string valid_array = "[{\"value\":1},true,null,\"text\"]";
  const std::string malformed =
      "{\"agent\":,\"interventions\":[],\"seed_strategies\":[]}";
  const std::string nested_only =
      "{\"meta\":{\"agent\":\"SmokeAgent\",\"interventions\":[],\"seed_strategies\":[]}}";
  const std::string string_only =
      "{\"note\":\"\\\"agent\\\":\\\"SmokeAgent\\\",\\\"interventions\\\":[],"
      "\\\"seed_strategies\\\":[]\"}";
  const std::string truncated =
      "{\"agent\":\"SmokeAgent\",\"interventions\":[{\"action\":\"dictionary_probe\"}";
  const std::string default_proposal_schema =
      "{\"required\":[\"agent\",\"interventions\",\"seed_strategies\"]}";
  auto satisfies_default_proposal_schema = [&](const std::string& json) {
    return fuzzpilot::json_object_satisfies_required_schema(json,
                                                            default_proposal_schema);
  };

  if (!fuzzpilot::is_complete_json_value(valid) ||
      !satisfies_default_proposal_schema(valid)) {
    std::cerr << "valid agent proposal rejected\n";
    return 1;
  }
  if (!fuzzpilot::is_complete_json_value(valid_array)) {
    std::cerr << "valid JSON array rejected\n";
    return 2;
  }
  if (fuzzpilot::is_complete_json_value(truncated) ||
      satisfies_default_proposal_schema(truncated)) {
    std::cerr << "truncated proposal accepted\n";
    return 3;
  }
  if (fuzzpilot::is_complete_json_value(malformed) ||
      satisfies_default_proposal_schema(malformed)) {
    std::cerr << "malformed proposal accepted\n";
    return 4;
  }
  if (satisfies_default_proposal_schema(nested_only)) {
    std::cerr << "nested-only proposal keys accepted\n";
    return 5;
  }
  if (satisfies_default_proposal_schema(string_only)) {
    std::cerr << "string-only proposal keys accepted\n";
    return 6;
  }
  const std::string result_analysis =
      "{\"agent\":\"ResultAnalysisAgent\",\"memory_patch\":{\"kind\":\"m6\"},"
      "\"critique\":\"winner looks useful\"}";
  const std::string result_analysis_schema =
      "{\"required\":[\"agent\",\"memory_patch\",\"critique\"]}";
  if (!fuzzpilot::json_object_satisfies_required_schema(result_analysis,
                                                       result_analysis_schema)) {
    std::cerr << "result analysis schema rejected\n";
    return 7;
  }
  if (satisfies_default_proposal_schema(result_analysis)) {
    std::cerr << "result analysis accepted as default proposal schema\n";
    return 8;
  }
  const auto memory_patch = fuzzpilot::extract_top_level_json_value(
      "{\"agent\":\"SmokeAgent\",\"memory_patch\":{\"kind\":\"m6\",\"confidence\":0.9},"
      "\"interventions\":[],\"seed_strategies\":[]}",
      "memory_patch");
  if (!memory_patch || *memory_patch != "{\"kind\":\"m6\",\"confidence\":0.9}") {
    std::cerr << "top-level memory_patch extraction failed\n";
    return 9;
  }
  if (fuzzpilot::extract_top_level_json_value(
          "{\"outer\":{\"memory_patch\":{\"kind\":\"nested\"}}}", "memory_patch")) {
    std::cerr << "nested memory_patch was extracted as top-level\n";
    return 10;
  }

  fuzzpilot::AgentDecision decision;
  decision.id = "decision_json_smoke";
  decision.run_id = "run_json_smoke";
  decision.plateau_id = "plateau_json_smoke";
  decision.agent = "SmokeAgent";
  decision.model_response.provider = "fake";
  decision.model_response.model = "fake";
  decision.model_response.context_hash = "ctx";
  decision.model_response.response_hash = "resp";
  decision.model_response.schema_valid = false;
  decision.proposal_json = truncated;

  const auto serialized = fuzzpilot::agent_decision_json(decision);
  if (serialized.find("\"proposal\":{\"raw\":\"") == std::string::npos) {
    std::cerr << "invalid proposal was not wrapped as raw text\n";
    return 11;
  }

  std::cout << "json utils smoke passed\n";
  return 0;
}
