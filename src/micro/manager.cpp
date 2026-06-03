#include "fuzzpilot/micro/manager.hpp"
#include "fuzzpilot/string_util.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/mutation/recipe_store.hpp"
#include "fuzzpilot/mutation/strategy.hpp"
#include "fuzzpilot/storage/db.hpp"
#include "fuzzpilot/json_utils.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {
namespace {

constexpr uint64_t kMaxSnapshotFiles = 4096;
constexpr uint64_t kMaxSnapshotFileBytes = 16ull * 1024ull * 1024ull;

std::filesystem::path find_queue_dir(const std::filesystem::path& afl_output_dir) {
  const auto direct = afl_output_dir / "queue";
  if (std::filesystem::is_directory(direct)) {
    return direct;
  }
  const auto default_worker = afl_output_dir / "default" / "queue";
  if (std::filesystem::is_directory(default_worker)) {
    return default_worker;
  }
  if (std::filesystem::is_directory(afl_output_dir)) {
    return afl_output_dir;
  }
  return {};
}
std::vector<std::string> extract_json_string_array(const std::string& object_json,
                                                   const std::string& key) {
  return extract_string_array_field(object_json, key);
}

std::vector<std::string> merge_tokens(std::vector<std::string> primary,
                                      const std::vector<std::string>& fallback) {
  for (const auto& token : fallback) {
    if (!token.empty() && std::find(primary.begin(), primary.end(), token) == primary.end()) {
      primary.push_back(token);
    }
  }
  return primary;
}

std::string sanitize_path_component(std::string value) {
  for (char& c : value) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) c = '_';
  }
  if (value.empty()) return "unknown";
  return value;
}

std::string extract_json_string_field(const std::string& object_json, const std::string& key) {
  auto value = extract_top_level_json_value(object_json, key);
  if (!value || value->size() < 2 || value->front() != '"' || value->back() != '"') {
    return "";
  }
  std::string out;
  bool escaped = false;
  for (std::size_t i = 1; i + 1 < value->size(); ++i) {
    const char c = (*value)[i];
    if (escaped) {
      switch (c) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        default: out.push_back(c); break;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

int infer_spiral_stage(const std::string& action) {
  if (action == "default_control") return 0;
  if (action == "dictionary_probe") return 1;
  if (action == "seed_focus_probe") return 2;
  if (action == "per_seed_recipe_probe") return 3;
  return 2;
}

double default_priority_for_action(const std::string& action) {
  if (action == "default_control") return 1.0;
  if (action == "dictionary_probe") return 0.9;
  if (action == "seed_focus_probe") return 0.8;
  if (action == "per_seed_recipe_probe") return 0.7;
  return 0.5;
}

int extract_json_int_field(const std::string& object_json,
                           const std::string& key,
                           int fallback) {
  const auto value = extract_top_level_json_value(object_json, key);
  if (!value) return fallback;
  std::string raw = trim(*value);
  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
    raw = extract_json_string_field(object_json, key);
  }
  try {
    return std::stoi(raw);
  } catch (...) {
    return fallback;
  }
}

double extract_json_double_field(const std::string& object_json,
                                 const std::string& key,
                                 double fallback) {
  const auto value = extract_top_level_json_value(object_json, key);
  if (!value) return fallback;
  std::string raw = trim(*value);
  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
    raw = extract_json_string_field(object_json, key);
  }
  try {
    return std::stod(raw);
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> split_top_level_array_objects(const std::string& array_json) {
  std::vector<std::string> objects;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t start = std::string::npos;
  for (std::size_t i = 0; i < array_json.size(); ++i) {
    const char c = array_json[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '{') {
      if (depth == 0) start = i;
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0 && start != std::string::npos) {
        objects.push_back(array_json.substr(start, i - start + 1));
        start = std::string::npos;
      }
    }
  }
  return objects;
}

std::string params_json_for_intervention(const RecentAgentDecision& decision,
                                         std::size_t index,
                                         const std::string& object_json,
                                         int spiral_stage,
                                         double priority,
                                         const std::string& depends_on_intervention_id) {
  return std::string("{\"source_decision_id\":\"") + json_escape(decision.id) +
         "\",\"source_agent\":\"" + json_escape(decision.agent) +
         "\",\"intervention_index\":" + std::to_string(index) +
         ",\"spiral_stage\":" + std::to_string(spiral_stage) +
         ",\"priority\":" + std::to_string(priority) +
         ",\"depends_on_intervention_id\":\"" +
         json_escape(depends_on_intervention_id) + "\"" +
         ",\"proposal\":" + json_value_or_raw(object_json) + "}";
}

std::string json_string_array(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) out << ",";
    out << "\"" << json_escape(values[i]) << "\"";
  }
  out << "]";
  return out.str();
}

std::string join_csv(const std::vector<std::string>& values) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) out << ",";
    out << values[i];
  }
  return out.str();
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
  if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

std::string representative_action_for_stage(int stage) {
  if (stage <= 0) return "default_control";
  if (stage == 1) return "dictionary_probe";
  if (stage == 2) return "seed_focus_probe";
  if (stage == 3) return "per_seed_recipe_probe";
  return "seed_focus_probe";
}

Intervention coalesce_stage_interventions(const std::vector<Intervention>& inputs,
                                          int stage,
                                          const Intervention* base_control) {
  Intervention out;
  if (stage <= 0 && base_control != nullptr) {
    out = *base_control;
  } else if (!inputs.empty()) {
    out = inputs.front();
    out.id = make_id("intv_spiral");
    out.action = representative_action_for_stage(stage);
    out.agent = inputs.size() == 1 ? inputs.front().agent : "AgentCouncil";
    out.hypothesis = "Aggregated spiral stage " + std::to_string(stage) +
                     " from " + std::to_string(inputs.size()) +
                     " agent proposal(s)";
    out.expected_signal = "new_edges";
    out.tokens.clear();
    out.source_decision_id.clear();
    out.params_json.clear();
  } else if (base_control != nullptr) {
    out = *base_control;
  }

  std::vector<std::string> source_agents;
  std::vector<std::string> source_decisions;
  std::vector<std::string> contributions;
  double priority = out.priority;
  for (const auto& input : inputs) {
    append_unique(source_agents, input.agent);
    append_unique(source_decisions, input.source_decision_id);
    for (const auto& token : input.tokens) {
      append_unique(out.tokens, token);
    }
    if (input.priority > priority) {
      priority = input.priority;
    }
    if (!input.params_json.empty()) {
      contributions.push_back(json_value_or_raw(input.params_json));
    }
  }

  out.spiral_stage = stage;
  out.priority = priority;
  if (!source_decisions.empty()) {
    out.source_decision_id = source_decisions.size() == 1
        ? source_decisions.front()
        : join_csv(source_decisions);
  }
  if (!source_agents.empty() && inputs.size() == 1) {
    out.agent = source_agents.front();
  } else if (!source_agents.empty()) {
    out.agent = "AgentCouncil";
  }

  if (!inputs.empty()) {
    std::ostringstream params;
    params << "{\"aggregation\":\"spiral_stage\","
           << "\"spiral_stage\":" << stage << ","
           << "\"action\":\"" << json_escape(out.action) << "\","
           << "\"source_agents\":" << json_string_array(source_agents) << ","
           << "\"source_decision_ids\":" << json_string_array(source_decisions)
           << ",\"contributions\":[";
    for (std::size_t i = 0; i < contributions.size(); ++i) {
      if (i != 0) params << ",";
      params << contributions[i];
    }
    params << "]}";
    out.params_json = params.str();
  }
  return out;
}

std::vector<Intervention> interventions_from_decision(const RecentAgentDecision& decision) {
  std::vector<Intervention> interventions;
  std::string interventions_json;
  auto interventions_array = extract_top_level_json_value(decision.proposal_json, "interventions");
  if (interventions_array) {
    interventions_json = *interventions_array;
  } else {
    auto answer_obj = extract_top_level_json_value(decision.proposal_json, "answer");
    if (answer_obj) {
      auto nested_array = extract_top_level_json_value(*answer_obj, "interventions");
      if (nested_array) interventions_json = *nested_array;
    }
  }
  if (interventions_json.empty()) return interventions;

  const auto objects = split_top_level_array_objects(interventions_json);
  for (std::size_t i = 0; i < objects.size(); ++i) {
    const auto& object_json = objects[i];
    const auto action = extract_json_string_field(object_json, "action");
    if (action.empty()) continue;
    Intervention intv;
    intv.id = make_id("intv_llm");
    intv.action = action;
    intv.agent = decision.agent;
    intv.hypothesis = extract_json_string_field(object_json, "hypothesis");
    if (intv.hypothesis.empty()) intv.hypothesis = "LLM-generated intervention";
    const auto expected = extract_json_string_field(object_json, "expected_signal");
    if (!expected.empty()) intv.expected_signal = expected;
    intv.tokens = extract_json_string_array(object_json, "tokens");
    intv.source_decision_id = decision.id;
    intv.spiral_stage = extract_json_int_field(
        object_json, "spiral_stage", infer_spiral_stage(action));
    intv.priority = extract_json_double_field(
        object_json, "priority", default_priority_for_action(action));
    intv.depends_on_intervention_id =
        extract_json_string_field(object_json, "depends_on_intervention_id");
    intv.params_json = params_json_for_intervention(
        decision, i, object_json, intv.spiral_stage, intv.priority,
        intv.depends_on_intervention_id);
    intv.risk = "medium";
    intv.reproducible = true;
    interventions.push_back(std::move(intv));
  }
  return interventions;
}

}  // namespace

CorpusSnapshotResult snapshot_corpus(const std::filesystem::path& afl_output_dir,
                                     const std::filesystem::path& snapshot_dir) {
  const auto queue_dir = find_queue_dir(afl_output_dir);
  if (queue_dir.empty()) {
    throw std::runtime_error("failed to find AFL++ queue under: " + afl_output_dir.string());
  }

  std::filesystem::create_directories(snapshot_dir);
  CorpusSnapshotResult result;
  result.source_queue = queue_dir;
  result.snapshot_dir = snapshot_dir;

  for (const auto& entry : std::filesystem::directory_iterator(queue_dir)) {
    if (result.files_copied >= kMaxSnapshotFiles) {
      break;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(entry.symlink_status(ec)) || ec) {
      continue;
    }
    const auto filename = entry.path().filename().string();
    if (!filename.empty() && filename[0] == '.') {
      continue;
    }
    const auto size = std::filesystem::file_size(entry.path(), ec);
    if (ec || size > kMaxSnapshotFileBytes) {
      continue;
    }
    const auto dest = snapshot_dir / entry.path().filename();
    std::filesystem::copy_file(entry.path(), dest,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
      continue;
    }
    ++result.files_copied;
    result.bytes_copied += size;
  }
  if (result.files_copied == 0) {
    throw std::runtime_error("queue snapshot copied zero files from: " + queue_dir.string());
  }
  return result;
}

std::vector<MicroCampaignSpec> plan_micro_campaigns(const AppConfig& config,
                                                    const std::string& plateau_id,
                                                    const std::filesystem::path& snapshot_dir,
                                                    const std::filesystem::path& work_dir,
                                                    bool dry_run,
                                                    Database* db,
                                                    const std::string& run_id) {
  std::vector<MicroCampaignSpec> specs;

  // Try to use LLM-generated decisions from database if available.
  std::vector<Intervention> agent_interventions;
  if (db != nullptr && !run_id.empty()) {
    std::cerr << "[DEBUG] Attempting to load schema-valid LLM decisions from database for run_id=" << run_id << "\n";
    const auto decisions = db->get_recent_agent_decisions(run_id, 40);
    std::cerr << "[DEBUG] get_recent_agent_decisions returned " << decisions.size() << " decisions\n";
    std::set<std::string> seen;
    for (const auto& decision : decisions) {
      auto extracted = interventions_from_decision(decision);
      for (auto& intv : extracted) {
        const std::string key = intv.agent + "\n" + intv.action + "\n" + intv.params_json;
        if (seen.insert(key).second) {
          std::cerr << "[DEBUG] Extracted intervention agent=" << intv.agent
                    << " action=" << intv.action
                    << " stage=" << intv.spiral_stage
                    << " priority=" << intv.priority
                    << " decision=" << intv.source_decision_id << "\n";
          agent_interventions.push_back(std::move(intv));
        }
      }
    }
  }

  std::cerr << "[DEBUG] Extracted " << agent_interventions.size() << " interventions from LLM decisions\n";

  const auto defaults = default_v0_interventions(config.micro_campaign.budget_sec);
  std::vector<Intervention> interventions;

  // Fallback to default interventions if no LLM decisions found. With
  // LLM decisions, keep one stage-0 default_control as the control arm and
  // coalesce the agent council into one campaign per spiral stage. This
  // keeps every useful agent contribution traceable without exploding a
  // heartbeat into dozens of redundant micro campaigns.
  if (agent_interventions.empty()) {
    std::cerr << "[DEBUG] No LLM interventions found, using default interventions\n";
    interventions = defaults;
  } else {
    std::map<int, std::vector<Intervention>> by_stage;
    for (auto intv : agent_interventions) {
      if (intv.spiral_stage < 0 || intv.spiral_stage > 3) {
        intv.spiral_stage = infer_spiral_stage(intv.action);
      }
      by_stage[intv.spiral_stage].push_back(std::move(intv));
    }

    const Intervention* default_control = defaults.empty() ? nullptr : &defaults.front();
    interventions.push_back(
        coalesce_stage_interventions(by_stage[0], 0, default_control));
    for (int stage = 1; stage <= 3; ++stage) {
      const auto found = by_stage.find(stage);
      if (found != by_stage.end() && !found->second.empty()) {
        interventions.push_back(
            coalesce_stage_interventions(found->second, stage, nullptr));
      }
    }
  }

  std::stable_sort(interventions.begin(), interventions.end(),
                   [](const Intervention& lhs, const Intervention& rhs) {
                     if (lhs.spiral_stage != rhs.spiral_stage) {
                       return lhs.spiral_stage < rhs.spiral_stage;
                     }
                     return lhs.priority > rhs.priority;
                   });

  std::map<int, std::string> latest_by_stage;
  for (auto& intervention : interventions) {
    if (intervention.spiral_stage <= 0) {
      intervention.depends_on_intervention_id.clear();
    } else if (intervention.depends_on_intervention_id.empty()) {
      for (auto it = latest_by_stage.rbegin(); it != latest_by_stage.rend(); ++it) {
        if (it->first < intervention.spiral_stage) {
          intervention.depends_on_intervention_id = it->second;
          break;
        }
      }
    }
    if (!intervention.params_json.empty()) {
      intervention.params_json = std::string("{\"source_params\":") +
          json_value_or_raw(intervention.params_json) +
          ",\"spiral_stage\":" + std::to_string(intervention.spiral_stage) +
          ",\"priority\":" + std::to_string(intervention.priority) +
          ",\"depends_on_intervention_id\":\"" +
          json_escape(intervention.depends_on_intervention_id) + "\"}";
    }
    latest_by_stage[intervention.spiral_stage] = intervention.id;
  }

  // Save interventions to database
  if (db != nullptr) {
    std::cerr << "[DEBUG] Saving " << interventions.size() << " interventions to database\n";
    for (const auto& intervention : interventions) {
      db->insert_intervention(
          intervention.id,
          plateau_id,
          intervention.agent,
          intervention.action,
          intervention.params_json.empty() ? "{}" : intervention.params_json,
          intervention.hypothesis,
          intervention.expected_signal,
          "pending"
      );
    }
    std::cerr << "[DEBUG] Interventions saved to database\n";
  }

  for (const auto& intervention : interventions) {
    MicroCampaignSpec spec;
    spec.id = make_id("micro");
    spec.intervention_id = intervention.id;
    spec.name = intervention.action;
    spec.input_dir = snapshot_dir;
    const auto safe_agent = sanitize_path_component(intervention.agent);
    const auto safe_action = sanitize_path_component(intervention.action);
    const auto safe_id = sanitize_path_component(intervention.id);
    spec.output_dir = work_dir / plateau_id / (safe_agent + "_" + safe_action + "_" + safe_id) / "out";
    spec.recipe_store = work_dir / plateau_id / (safe_agent + "_" + safe_action + "_" + safe_id) / "recipes";
    spec.tokens = intervention.tokens;
    spec.agent = intervention.agent;
    spec.source_decision_id = intervention.source_decision_id;
    spec.params_json = intervention.params_json;
    spec.spiral_stage = intervention.spiral_stage;
    spec.priority = intervention.priority;
    spec.depends_on_intervention_id = intervention.depends_on_intervention_id;
    spec.budget_sec = static_cast<uint32_t>(config.micro_campaign.budget_sec);
    spec.dry_run = dry_run;
    specs.push_back(std::move(spec));
  }
  return specs;
}

void prepare_micro_campaigns(const std::vector<MicroCampaignSpec>& specs,
                             const std::vector<std::string>& llm_tokens) {
  for (const auto& spec : specs) {
    std::filesystem::create_directories(spec.output_dir);
    RecipeStore store(spec.recipe_store);

    std::vector<std::string> tokens = merge_tokens(spec.tokens, llm_tokens);
    if (tokens.empty()) {
      tokens = {"FUZZ", "MAGIC", "TOKEN"};
    }

    std::vector<SeedMutationStrategy> strategies;
    if (spec.name == "default_control") {
      // intv_default: default AFL++ behavior (no recipe)
    } else if (spec.name == "dictionary_probe") {
      // intv_dictionary: use LLM dictionary tokens
      strategies.push_back(make_default_dictionary_strategy(tokens));
    } else if (spec.name == "seed_focus_probe") {
      // intv_seed_focus: use LLM tokens + seed-specific offset
      for (const auto& entry : std::filesystem::directory_iterator(spec.input_dir)) {
        if (std::filesystem::is_regular_file(entry.path())) {
          std::string seed_id = entry.path().filename().string();
          strategies.push_back(make_seed_focus_strategy(seed_id, tokens));
        }
      }
      if (strategies.empty()) {
        strategies.push_back(make_default_dictionary_strategy(tokens));
      }
    } else if (spec.name == "per_seed_recipe_probe") {
      // intv_per_seed_recipe: use per-seed LLM token
      for (const auto& entry : std::filesystem::directory_iterator(spec.input_dir)) {
        if (std::filesystem::is_regular_file(entry.path())) {
          strategies.push_back(make_seed_hash_strategy(entry.path(), tokens));
        }
      }
      if (strategies.empty()) {
        strategies.push_back(make_default_dictionary_strategy(tokens));
      }
    } else {
      strategies.push_back(make_default_dictionary_strategy(tokens));
    }

    if (!strategies.empty()) {
      store.write_compact_recipes(strategies);
    }
    std::ofstream manifest(spec.output_dir.parent_path() / "campaign.json");
    manifest << micro_campaign_spec_json(spec) << "\n";
  }
}

std::vector<MicroBanditRank> rank_micro_bandit_candidates(
    const std::vector<MicroBanditCandidate>& candidates,
    double total_budget_sec,
    double exploration_c) {
  std::vector<MicroBanditRank> ranks;
  ranks.reserve(candidates.size());

  const double safe_total = std::max(1.0, total_budget_sec);
  const double safe_exploration = std::max(0.0, exploration_c);
  for (const auto& candidate : candidates) {
    const double denom = static_cast<double>(candidate.budget_sec) + 1.0;
    const double explore =
        safe_exploration * std::sqrt(std::log(safe_total + 1.0) / denom);
    ranks.push_back({candidate.campaign_id, candidate.mean_reward + explore});
  }

  std::sort(ranks.begin(), ranks.end(),
            [](const MicroBanditRank& lhs, const MicroBanditRank& rhs) {
              if (std::fabs(lhs.score - rhs.score) >
                  std::numeric_limits<double>::epsilon()) {
                return lhs.score > rhs.score;
              }
              return lhs.campaign_id < rhs.campaign_id;
            });
  return ranks;
}

std::string corpus_snapshot_json(const CorpusSnapshotResult& snapshot) {
  std::ostringstream out;
  out << "{";
  out << "\"source_queue\":\"" << json_escape(snapshot.source_queue.string()) << "\",";
  out << "\"snapshot_dir\":\"" << json_escape(snapshot.snapshot_dir.string()) << "\",";
  out << "\"files_copied\":" << snapshot.files_copied << ",";
  out << "\"bytes_copied\":" << snapshot.bytes_copied;
  out << "}";
  return out.str();
}

std::string micro_campaign_spec_json(const MicroCampaignSpec& spec) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << json_escape(spec.id) << "\",";
  out << "\"intervention_id\":\"" << json_escape(spec.intervention_id) << "\",";
  out << "\"name\":\"" << json_escape(spec.name) << "\",";
  out << "\"input_dir\":\"" << json_escape(spec.input_dir.string()) << "\",";
  out << "\"output_dir\":\"" << json_escape(spec.output_dir.string()) << "\",";
  out << "\"recipe_store\":\"" << json_escape(spec.recipe_store.string()) << "\",";
  out << "\"agent\":\"" << json_escape(spec.agent) << "\",";
  out << "\"source_decision_id\":\"" << json_escape(spec.source_decision_id) << "\",";
  out << "\"spiral_stage\":" << spec.spiral_stage << ",";
  out << "\"priority\":" << spec.priority << ",";
  out << "\"depends_on_intervention_id\":\"" << json_escape(spec.depends_on_intervention_id) << "\",";
  out << "\"params\":" << (spec.params_json.empty() ? "{}" : json_value_or_raw(spec.params_json)) << ",";
  out << "\"token_count\":" << spec.tokens.size() << ",";
  out << "\"budget_sec\":" << spec.budget_sec << ",";
  out << "\"dry_run\":" << (spec.dry_run ? "true" : "false");
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
