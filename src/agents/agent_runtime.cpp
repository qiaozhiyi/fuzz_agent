#include "fuzzpilot/agents/agent_runtime.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/string_util.hpp"

#include <ctime>
#include <sstream>

namespace fuzzpilot {
namespace {

std::string json_value_or_raw(const std::string& value) {
  if (value.empty()) {
    return "{}";
  }
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && (value[first] == '{' || value[first] == '[')) {
    return value;
  }
  return std::string("{\"raw\":\"") + json_escape(value) + "\"}";
}

}  // namespace

std::vector<AgentTask> make_default_agent_tasks(const std::string& plateau_id,
                                                const std::string& blackboard_json,
                                                uint32_t budget_sec) {
  const std::vector<std::string> agents = {
      "CoordinatorAgent",
      "PlateauDiagnosisAgent",
      "SchedulerAgent",
      "CmpAgent",
      "MutatorAgent",
      "DictionaryAgent",
      "FormatAgent",
      "CorpusAgent",
  };
  std::vector<AgentTask> tasks;
  for (const auto& agent : agents) {
    AgentTask task;
    task.task_id = make_id("agent_task");
    task.agent_name = agent;
    task.objective = "Generate typed FuzzPilot proposal for plateau " + plateau_id;
    task.blackboard_slice_json = blackboard_json;
    task.action_schema_json =
        "{\"allowed_actions\":[\"default_control\",\"dictionary_probe\","
        "\"seed_focus_probe\",\"per_seed_recipe_probe\"]}";
    task.output_schema_json =
        "{\"required\":[\"agent\",\"interventions\",\"seed_strategies\"]}";
    task.budget_sec = budget_sec;
    tasks.push_back(std::move(task));
  }
  return tasks;
}

std::vector<AgentDecision> run_agent_tasks(IModelGateway& gateway,
                                           const std::string& run_id,
                                           const std::string& plateau_id,
                                           const std::vector<AgentTask>& tasks) {
  std::vector<AgentDecision> decisions;
  for (const auto& task : tasks) {
    ModelRequest request;
    request.agent_name = task.agent_name;
    request.system_prompt =
        "You are a FuzzPilot model-backed agent. You are part of an Advanced Agentic Fuzzing loop (M4+). "
        "When binary intelligence (static_context) is available, you must leverage it to orchestrate 'Structural & Semantic Mutation': \n"
        "1. **Structural Fields**: Use identified struct layouts to define semantic fields in your 'seed_strategies' or 'per_seed_recipe_probe'. \n"
        "   - Specify `fields` with `type`: 'Length' (to enable automatic repair), 'Magic' (to lock values), or 'Checksum'.\n"
        "   - For 'Length' fields, define `target_begin` and `target_end` to tell the mutator which range this length describes.\n"
        "2. **Data-flow Mapping**: If IDA identifies that a specific offset controls a sink (e.g., malloc size or strcpy), focus mutations on that 'DATA' field.\n"
        "3. **CFG Constraints**: For unreached branches, analyze the required 'magic' comparisons at the reported addresses and propose the exact values.\n"
        "Always justify your strategy based on the structural metadata (e.g., 'Targeting the length field at offset 0x04 to bypass size checks'). "
        "Return valid compact JSON following the requested output schema.";
    request.user_context_json = agent_task_json(task);
    request.output_schema_json = task.output_schema_json;
    request.timeout_ms = task.timeout_ms;
    request.max_output_tokens = 1024;

    AgentDecision decision;
    decision.id = make_id("agent_decision");
    decision.run_id = run_id;
    decision.plateau_id = plateau_id;
    decision.agent = task.agent_name;
    decision.task_json = agent_task_json(task);
    decision.model_response = gateway.complete_json(request);
    decision.proposal_json = decision.model_response.response_json;
    decision.created_ts = static_cast<uint64_t>(std::time(nullptr));
    decisions.push_back(std::move(decision));
  }
  return decisions;
}

std::string agent_task_json(const AgentTask& task) {
  std::ostringstream out;
  out << "{";
  out << "\"task_id\":\"" << json_escape(task.task_id) << "\",";
  out << "\"agent_name\":\"" << json_escape(task.agent_name) << "\",";
  out << "\"objective\":\"" << json_escape(task.objective) << "\",";
  out << "\"blackboard_slice\":" << (task.blackboard_slice_json.empty() ? "{}" : task.blackboard_slice_json) << ",";
  out << "\"action_schema\":" << (task.action_schema_json.empty() ? "{}" : task.action_schema_json) << ",";
  out << "\"output_schema\":" << (task.output_schema_json.empty() ? "{}" : task.output_schema_json) << ",";
  out << "\"budget_sec\":" << task.budget_sec << ",";
  out << "\"timeout_ms\":" << task.timeout_ms;
  out << "}";
  return out.str();
}

std::string agent_decision_json(const AgentDecision& decision) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << json_escape(decision.id) << "\",";
  out << "\"run_id\":\"" << json_escape(decision.run_id) << "\",";
  out << "\"plateau_id\":\"" << json_escape(decision.plateau_id) << "\",";
  out << "\"agent\":\"" << json_escape(decision.agent) << "\",";
  out << "\"provider\":\"" << json_escape(decision.model_response.provider) << "\",";
  out << "\"model\":\"" << json_escape(decision.model_response.model) << "\",";
  out << "\"context_hash\":\"" << json_escape(decision.model_response.context_hash) << "\",";
  out << "\"response_hash\":\"" << json_escape(decision.model_response.response_hash) << "\",";
  out << "\"latency_ms\":" << decision.model_response.latency_ms << ",";
  out << "\"schema_valid\":" << (decision.model_response.schema_valid ? "true" : "false") << ",";
  out << "\"fallback_used\":" << (decision.fallback_used ? "true" : "false") << ",";
  out << "\"proposal\":" << json_value_or_raw(decision.proposal_json);
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
