#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/string_util.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/json_utils.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <set>
#include <sstream>

namespace fuzzpilot {
namespace {

// Per-agent role descriptions injected into the system prompt so each
// agent genuinely behaves differently (rather than just being labeled).
std::string role_for(const std::string& agent_name) {
  if (agent_name == "CoordinatorAgent") {
    return "Reconcile other agents' proposals into a coherent execution plan; "
           "your job is to choose at most three high-leverage interventions to validate.";
  }
  if (agent_name == "PlateauDiagnosisAgent") {
    return "Diagnose why coverage has stalled. Cite the most likely cause "
           "(cmp gate / sparse dictionary / structural barrier / energy mis-allocation / "
           "corpus saturation) and rank by evidence in the blackboard.";
  }
  if (agent_name == "SchedulerAgent") {
    return "Reallocate seed energy. Identify under-explored seeds and propose "
           "favored-set adjustments; do not propose mutation operators.";
  }
  if (agent_name == "CmpAgent") {
    return "Find comparison/magic-value bottlenecks. Propose targeted token "
           "and offset focus to satisfy specific compare branches.";
  }
  if (agent_name == "MutatorAgent") {
    return "Propose per-seed operator weights and offset ranges. Stay within "
           "the allowed action set; never invent new operators.";
  }
  if (agent_name == "DictionaryAgent") {
    return "Suggest new dictionary entries (magic numbers, format keywords, "
           "ASCII tokens) extracted from static_context or known formats.";
  }
  if (agent_name == "FormatAgent") {
    return "Repair or preserve structural integrity. Identify length/checksum/"
           "magic fields and propose repair strategies.";
  }
  if (agent_name == "CorpusAgent") {
    return "Curate the corpus: deduplication, minimization, removal of "
           "redundant seeds. Do not propose new mutations.";
  }
  return "Generic FuzzPilot proposing agent.";
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
    task.role_description = role_for(agent);
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

std::vector<AgentTask> filter_disabled_agents(std::vector<AgentTask> tasks,
                                              const std::vector<std::string>& disabled) {
  if (disabled.empty()) {
    return tasks;
  }
  std::set<std::string> drop(disabled.begin(), disabled.end());
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                             [&drop](const AgentTask& t) {
                               return drop.count(t.agent_name) > 0;
                             }),
              tasks.end());
  return tasks;
}

bool validate_agent_proposal(const std::string& proposal_json,
                             const std::string& action_schema_json,
                             std::string* reason) {
  // NOTE: do NOT re-check the per-task output schema here. The gateway
  // already validated it against `task.output_schema_json`; redoing it
  // with a hardcoded shape was a regression that rejected legitimate
  // proposals from agents whose schema doesn't require `interventions`
  // (e.g. ResultAnalysisAgent emits a `memory_patch` + `critique` shape).
  //
  // This function's job is purely the *guardrail* layer: action-space
  // membership + numeric range sanity. Schema correctness is the
  // gateway's responsibility.

  // Pull allowed_actions list out of the schema.
  std::vector<std::string> allowed = extract_string_array_field(action_schema_json,
                                                                "allowed_actions");
  if (allowed.empty()) {
    // No constraint shipped — degrade gracefully (the caller already
    // logged a warning earlier in setup).
    return true;
  }
  // Walk through every "action" string inside `interventions` and verify
  // membership. The walk is intentionally lenient: it permits any extra
  // whitespace, but rejects unknown actions outright. `interventions`
  // is treated as OPTIONAL — agents like ResultAnalysisAgent legitimately
  // emit a different shape (memory_patch + critique) and should not be
  // rejected. The schema-shape check is the gateway's job; here we only
  // enforce the action-space whitelist for the agents that DO emit
  // interventions.
  std::size_t cursor = proposal_json.find("\"interventions\"");
  if (cursor == std::string::npos) {
    // No interventions field — skip whitelist check, proceed to numeric
    // sanity checks below.
  } else {
    cursor = proposal_json.find('[', cursor);
    if (cursor == std::string::npos) {
      if (reason) *reason = "interventions present but not an array";
      return false;
    }
    const std::size_t end = proposal_json.find(']', cursor);
    if (end == std::string::npos) {
      if (reason) *reason = "interventions array not closed";
      return false;
    }
    const std::string slice = proposal_json.substr(cursor, end - cursor);
    // Scan for "action":"..." pairs.
    std::size_t pos = 0;
    while ((pos = slice.find("\"action\"", pos)) != std::string::npos) {
      pos = slice.find(':', pos);
      if (pos == std::string::npos) break;
      pos = slice.find('"', pos);
      if (pos == std::string::npos) break;
      const std::size_t start = pos + 1;
      const std::size_t stop = slice.find('"', start);
      if (stop == std::string::npos) break;
      const std::string action = slice.substr(start, stop - start);
      if (std::find(allowed.begin(), allowed.end(), action) == allowed.end()) {
        if (reason) *reason = "intervention action not in allow-list: " + action;
        return false;
      }
      pos = stop + 1;
    }
  }
  // Cheap numeric-field sanity check — reject obviously pathological
  // values that would break the mutator. NOTE: scans the ENTIRE
  // proposal_json text (not just top level) so an adversarial LLM
  // can't bypass the cap by nesting the numeric inside
  // `{"interventions":[{"fields":[{"target_begin":1e10}]}]}`. The
  // string search hits all occurrences of the key regardless of depth.
  // The trade-off is false positives if a legitimate value happens to
  // contain the substring inside another field name — we accept that
  // because the keys (target_begin/target_end/budget/timeout_ms) are
  // distinctive enough that collisions in well-formed JSON are
  // unrealistic in practice. If a key name was to be reused (e.g. an
  // agent emits "budget" as a string label inside a different
  // context), the only consequence is a false-positive reject which
  // is the safer failure mode for a guardrail.
  const std::vector<std::pair<std::string, uint64_t>> numeric_caps = {
      {"target_begin", 1ull << 28},   // 256 MB offset cap
      {"target_end",   1ull << 28},
      {"budget",       3600ull * 24}, // 24h budget cap
      {"timeout_ms",   600ull * 1000},
  };
  for (const auto& [key, cap] : numeric_caps) {
    std::size_t hpos = 0;
    while ((hpos = proposal_json.find("\"" + key + "\"", hpos)) != std::string::npos) {
      const auto colon = proposal_json.find(':', hpos);
      if (colon == std::string::npos) break;
      std::size_t numStart = colon + 1;
      while (numStart < proposal_json.size() &&
             std::isspace(static_cast<unsigned char>(proposal_json[numStart]))) {
        ++numStart;
      }
      const bool negative = numStart < proposal_json.size() && proposal_json[numStart] == '-';
      if (negative) {
        if (reason) *reason = "negative " + key;
        return false;
      }
      std::size_t numEnd = numStart;
      while (numEnd < proposal_json.size() &&
             (std::isdigit(static_cast<unsigned char>(proposal_json[numEnd])) ||
              proposal_json[numEnd] == 'e' || proposal_json[numEnd] == 'E' ||
              proposal_json[numEnd] == '.' || proposal_json[numEnd] == '+')) {
        ++numEnd;
      }
      if (numEnd > numStart) {
        try {
          const double v = std::stod(proposal_json.substr(numStart, numEnd - numStart));
          if (v > static_cast<double>(cap)) {
            if (reason) *reason = key + " out of range: " + proposal_json.substr(numStart, numEnd - numStart);
            return false;
          }
        } catch (...) {
          // ignore — malformed; will be caught upstream
        }
      }
      hpos = numEnd;
    }
  }
  return true;
}

std::vector<AgentDecision> run_agent_tasks(IModelGateway& gateway,
                                           const std::string& run_id,
                                           const std::string& plateau_id,
                                           const std::vector<AgentTask>& tasks) {
  std::vector<AgentDecision> decisions;
  for (const auto& task : tasks) {
    ModelRequest request;
    request.agent_name = task.agent_name;
    const std::string role = task.role_description.empty() ? role_for(task.agent_name)
                                                           : task.role_description;
    request.system_prompt =
        std::string("You are FuzzPilot's ") + task.agent_name + ". " + role +
        " You are part of an Advanced Agentic Fuzzing loop (M4+). "
        "When binary intelligence (static_context) is available, leverage it for "
        "'Structural & Semantic Mutation': "
        "1. Structural Fields: specify `fields` with type Length/Magic/Checksum and "
        "`target_begin`/`target_end` where applicable. "
        "2. Data-flow Mapping: focus mutations on offsets that reach sinks. "
        "3. CFG Constraints: for unreached branches propose the exact magic values. "
        "Justify the strategy based on structural metadata. "
        "Return strict compact JSON following the requested output schema. "
        "Do not include Markdown, comments, trailing commas, NaN/Infinity, or hex literals. "
        "Represent byte/address constants as quoted strings such as \"0xDEADBEEF\". "
        "Stay strictly within the action_schema.allowed_actions list — never invent new actions.";
    request.user_context_json = agent_task_json(task);
    request.output_schema_json = task.output_schema_json;
    request.timeout_ms = task.timeout_ms;
    request.max_output_tokens = task.max_output_tokens;

    AgentDecision decision;
    decision.id = make_id("agent_decision");
    decision.run_id = run_id;
    decision.plateau_id = plateau_id;
    decision.agent = task.agent_name;
    decision.task_json = agent_task_json(task);
    decision.model_response = gateway.complete_json(request);
    decision.proposal_json = decision.model_response.response_json;
    decision.created_ts = static_cast<uint64_t>(std::time(nullptr));

    // Guardrail layer: structural + action-space validation. A response
    // that passes the gateway's shallow schema check can still fail here,
    // in which case we drop it back to the rule fallback by flagging it.
    std::string reason;
    if (decision.model_response.schema_valid &&
        !validate_agent_proposal(decision.proposal_json,
                                 task.action_schema_json, &reason)) {
      decision.model_response.schema_valid = false;
      decision.model_response.error_kind = "guardrail_violation";
      decision.model_response.error = "guardrail: " + reason;
      decision.fallback_used = true;
    }
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
  out << "\"input_tokens\":" << decision.model_response.input_tokens << ",";
  out << "\"output_tokens\":" << decision.model_response.output_tokens << ",";
  out << "\"retry_count\":" << decision.model_response.retry_count << ",";
  out << "\"error_kind\":\"" << json_escape(decision.model_response.error_kind) << "\",";
  out << "\"schema_valid\":" << (decision.model_response.schema_valid ? "true" : "false") << ",";
  out << "\"fallback_used\":" << (decision.fallback_used ? "true" : "false") << ",";
  out << "\"proposal\":" << json_value_or_raw(decision.proposal_json);
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
