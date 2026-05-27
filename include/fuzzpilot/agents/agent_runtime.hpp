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
  // Per-agent role description. Embedded into the system prompt so the
  // model genuinely differentiates SchedulerAgent from MutatorAgent. If
  // empty, a default is generated from agent_name.
  std::string role_description;
  // Optional in-context few-shot block (already prose-formatted) appended
  // verbatim to the system prompt. Caller keeps it within context window.
  std::string few_shot_examples;
  uint32_t budget_sec = 0;
  uint32_t timeout_ms = 30000;
  uint32_t max_output_tokens = 1024;
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

// `few_shot_examples` (optional) is appended to every task's prompt so the
// LLM sees prior high/low-reward decisions. Format is caller's choice but a
// short prose block of GOOD/BAD bullets works well.
std::vector<AgentTask> make_default_agent_tasks(const std::string& plateau_id,
                                                const std::string& blackboard_json,
                                                uint32_t budget_sec,
                                                const std::string& few_shot_examples = "");

// Filter tasks to drop disabled agents — used by the ablation matrix to
// run "no Coordinator" / "no Mutator" experiments.
std::vector<AgentTask> filter_disabled_agents(std::vector<AgentTask> tasks,
                                              const std::vector<std::string>& disabled);

// Strict structural validation of an agent proposal against the action
// schema + numeric range guardrails. Returns true if the proposal is
// acceptable; reason is populated on failure for logging.
bool validate_agent_proposal(const std::string& proposal_json,
                             const std::string& action_schema_json,
                             std::string* reason);

// `deadline_unix_sec` is an absolute wall-clock cut-off (seconds since
// epoch). When non-zero, any agent that hasn't started by the deadline
// is recorded with error_kind="deadline_exceeded" instead of blocking
// on a slow LLM call. Default 0 = no cap (legacy behaviour).
std::vector<AgentDecision> run_agent_tasks(IModelGateway& gateway,
                                           const std::string& run_id,
                                           const std::string& plateau_id,
                                           const std::vector<AgentTask>& tasks,
                                           uint64_t deadline_unix_sec = 0);

std::string agent_task_json(const AgentTask& task);
std::string agent_decision_json(const AgentDecision& decision);

}  // namespace fuzzpilot
