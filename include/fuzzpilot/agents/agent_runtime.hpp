#pragma once

#include "fuzzpilot/model/gateway.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fuzzpilot {

struct AgentTask {
  std::string task_id;
  std::string agent_name;
  std::string objective;
  std::string blackboard_slice_json;
  std::string action_schema_json;
  std::string output_schema_json;
  uint32_t budget_sec = 0;
  uint32_t timeout_ms = 30000;
};

struct AgentDecision {
  std::string id;
  std::string run_id;
  std::string plateau_id;
  std::string agent;
  ModelResponse model_response;
  std::string task_json;
  std::string proposal_json;
  bool fallback_used = false;
  uint64_t created_ts = 0;
};

std::vector<AgentTask> make_default_agent_tasks(const std::string& plateau_id,
                                                const std::string& blackboard_json,
                                                uint32_t budget_sec);

std::vector<AgentDecision> run_agent_tasks(IModelGateway& gateway,
                                           const std::string& run_id,
                                           const std::string& plateau_id,
                                           const std::vector<AgentTask>& tasks);

std::string agent_task_json(const AgentTask& task);
std::string agent_decision_json(const AgentDecision& decision);

}  // namespace fuzzpilot

