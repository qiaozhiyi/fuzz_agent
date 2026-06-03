#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/json_utils.hpp"

#include <iostream>
#include <string>

namespace {

class PromptCheckingGateway final : public fuzzpilot::IModelGateway {
 public:
  fuzzpilot::ModelResponse complete_json(const fuzzpilot::ModelRequest& request) override {
    fuzzpilot::ModelResponse response;
    response.provider = "prompt-check";
    response.model = "fake";
    response.context_hash = fuzzpilot::stable_text_hash(request.system_prompt);

    const bool is_result_analysis = request.agent_name == "ResultAnalysisAgent";
    const bool asks_for_memory_patch =
        request.system_prompt.find("\"memory_patch\"") != std::string::npos;
    const bool asks_for_interventions =
        request.system_prompt.find("\"interventions\"") != std::string::npos;

    if (is_result_analysis && (!asks_for_memory_patch || asks_for_interventions)) {
      response.response_json = "{}";
      response.error_kind = "prompt_bad";
      response.error = "ResultAnalysisAgent prompt must request memory_patch, not interventions";
      response.schema_valid = false;
      return response;
    }

    response.response_json =
        "{\"agent\":\"ResultAnalysisAgent\","
        "\"memory_patch\":{\"winner\":\"intv_agent\"},"
        "\"critique\":\"micro winner is supported by reward delta\"}";
    response.schema_valid = fuzzpilot::json_object_satisfies_required_schema(
        response.response_json, request.output_schema_json);
    response.response_hash = fuzzpilot::stable_text_hash(response.response_json);
    return response;
  }
};

}  // namespace

int main() {
  fuzzpilot::AgentTask task;
  task.task_id = "task_result_analysis";
  task.agent_name = "ResultAnalysisAgent";
  task.objective = "Summarize micro campaign results";
  task.blackboard_slice_json = "{\"micro_campaign_results\":{\"winner\":\"intv_agent\"}}";
  task.action_schema_json =
      "{\"allowed_actions\":[\"memory_patch\",\"priority_update\",\"keep_winner\"]}";
  task.output_schema_json = "{\"required\":[\"agent\",\"memory_patch\",\"critique\"]}";

  PromptCheckingGateway gateway;
  const auto decisions = fuzzpilot::run_agent_tasks(
      gateway, "run_prompt_smoke", "plateau_prompt_smoke", {task});
  if (decisions.size() != 1) {
    std::cerr << "expected one result analysis decision\n";
    return 1;
  }
  if (!decisions.front().model_response.schema_valid) {
    std::cerr << decisions.front().model_response.error << "\n";
    return 2;
  }
  if (decisions.front().proposal_json.find("\"memory_patch\"") == std::string::npos) {
    std::cerr << "result analysis proposal lost memory_patch\n";
    return 3;
  }
  std::cout << "agent runtime smoke passed\n";
  return 0;
}
