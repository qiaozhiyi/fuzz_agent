#include "fuzzpilot/controller/run.hpp"
#include "fuzzpilot/string_util.hpp"

#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/config.hpp"
#include "fuzzpilot/env.hpp"
#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/json_utils.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/micro/manager.hpp"
#include "fuzzpilot/model/gateway.hpp"
#include "fuzzpilot/mutation/recipe_store.hpp"
#include "fuzzpilot/mutation/token_extractor.hpp"
#include "fuzzpilot/mutation/recipe_reward_tracker.hpp"
#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/runner/afl_runner.hpp"
#include "fuzzpilot/runner/process.hpp"
#include "fuzzpilot/storage/db.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"
#include "fuzzpilot/telemetry/collector.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <array>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace fuzzpilot {
namespace {

void append_line(const std::filesystem::path& path, const std::string& line) {
  std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  std::ofstream output(path, std::ios::app);
  if (!output) {
    throw std::runtime_error("failed to write event log: " + path.string());
  }
  output << line << "\n";
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("failed to write file: " + path.string());
  }
  output << content;
}

std::string telemetry_event_json(const std::string& run_id,
                                 const std::string& campaign_id,
                                 const AflStats& stats) {
  return std::string("{\"event\":\"telemetry_tick\",\"run_id\":\"") + json_escape(run_id) +
         "\",\"campaign_id\":\"" + json_escape(campaign_id) + "\",\"stats\":" +
         afl_stats_json(stats) + "}";
}

RewardMode reward_mode_from_string(const std::string& value) {
  if (value == "edges_only") return RewardMode::kEdgesOnly;
  if (value == "paths_only") return RewardMode::kPathsOnly;
  if (value == "random") return RewardMode::kRandom;
  return RewardMode::kEdgeWeighted;
}

std::string plateau_blackboard_json(const PlateauEvent& plateau, const AflStats& stats,
                                    const std::string& static_context_json = "{}",
                                    const std::vector<std::string>& recent_decisions = {},
                                    const std::vector<std::string>& memory_entries = {}) {
  std::string history = "[";
  for (size_t i = 0; i < recent_decisions.size(); ++i) {
    history += recent_decisions[i];
    if (i + 1 < recent_decisions.size()) history += ",";
  }
  history += "]";

  std::string memory = "[";
  for (size_t i = 0; i < memory_entries.size(); ++i) {
    memory += memory_entries[i];
    if (i + 1 < memory_entries.size()) memory += ",";
  }
  memory += "]";

  return std::string("{\"plateau\":") + plateau_event_json(plateau) +
         ",\"main_metrics\":" + afl_stats_json(stats) +
         ",\"static_analysis_context\":" + static_context_json +
         ",\"historical_context\":{\"recent_decisions\":" + history +
         ",\"agent_memory\":" + memory + "}" +
         ",\"available_actions\":[\"default_control\",\"dictionary_probe\","
         "\"seed_focus_probe\",\"per_seed_recipe_probe\"]}";
}

std::string static_analysis_error_json(const std::string& backend, const std::string& error) {
  return std::string("{\"backend\":\"") + json_escape(backend) +
         "\",\"error\":\"" + json_escape(error.substr(0, 1024)) + "\"}";
}

std::string read_text_or_error(const std::filesystem::path& path,
                               const std::string& backend) {
  std::ifstream ifs(path);
  if (!ifs) {
    return static_analysis_error_json(backend, "static analysis output not found: " + path.string());
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

std::string read_precomputed_static_context(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    throw std::runtime_error("failed to read static_analysis.context_path: " +
                             path.string());
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  auto content = ss.str();
  if (content.empty()) {
    throw std::runtime_error("static_analysis.context_path is empty: " +
                             path.string());
  }
  return content;
}

std::string run_static_extractor(const StaticAnalysisConfig& sa_config,
                                 const std::filesystem::path& target_binary,
                                 const std::filesystem::path& run_dir,
                                 const std::filesystem::path& output_json) {
  const auto backend = normalize_static_backend(sa_config.backend);
  if (backend != "ghidra") {
    return static_analysis_error_json(backend, "unsupported static analysis backend (only 'ghidra' is supported)");
  }

  const auto headless = resolve_ghidra_headless_path(sa_config);
  const auto project_dir = run_dir / "ghidra_project";
  std::filesystem::create_directories(project_dir);

  const auto script_dir = sa_config.extractor_script.parent_path().empty()
                              ? std::filesystem::path(".")
                              : sa_config.extractor_script.parent_path();
  const auto script_name = sa_config.extractor_script.filename();
  const auto project_name = "fuzzpilot_" + std::to_string(static_cast<uint64_t>(std::time(nullptr)));
  const auto timeout = std::to_string(std::max(1, sa_config.timeout_sec));

  const std::vector<std::string> argv = {
      headless.string(),
      project_dir.string(),
      project_name,
      "-import",
      target_binary.string(),
      "-scriptPath",
      script_dir.string(),
      "-postScript",
      script_name.string(),
      output_json.string(),
      "-analysisTimeoutPerFile",
      timeout,
      "-deleteProject",
  };

  const auto result = run_process_capture(headless.string(), argv, {}, true, 1024 * 1024,
                                          std::max(1, sa_config.timeout_sec + 30) * 1000);
  if (!result.spawned || !result.exited || result.exit_code != 0) {
    return static_analysis_error_json("ghidra",
                                      "ghidra extractor failed: " + result.error + " " + result.output);
  }
  return read_text_or_error(output_json, "ghidra");
}

std::string run_static_extractor(const StaticAnalysisConfig& sa_config,
                                 const std::filesystem::path& target_binary,
                                 const std::filesystem::path& run_dir) {
  const auto output_json = run_dir / "static_context.json";
  return run_static_extractor(sa_config, target_binary, run_dir, output_json);
}

std::filesystem::path generate_dict_from_static_json(const std::string& static_json,
                                                     const std::filesystem::path& run_dir) {
  const auto dict_path = run_dir / "static_generated.dict";
  std::ofstream dict_out(dict_path);
  if (!dict_out) return {};

  dict_out << "# Auto-generated by FuzzPilot from static analysis extraction\n";

  auto decode_json_string = [](const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    bool escaped = false;
    for (const char c : value) {
      if (escaped) {
        switch (c) {
          case 'n': decoded.push_back('\n'); break;
          case 'r': decoded.push_back('\r'); break;
          case 't': decoded.push_back('\t'); break;
          case '\\': decoded.push_back('\\'); break;
          case '"': decoded.push_back('"'); break;
          default: decoded.push_back(c); break;
        }
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else {
        decoded.push_back(c);
      }
    }
    if (escaped) {
      decoded.push_back('\\');
    }
    return decoded;
  };

  auto afl_dict_escape = [](const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char c : value) {
      if (c == '\\' || c == '"') {
        escaped.push_back('\\');
        escaped.push_back(static_cast<char>(c));
      } else if (c >= 0x20 && c <= 0x7e) {
        escaped.push_back(static_cast<char>(c));
      }
    }
    return escaped;
  };

  auto should_keep_token = [](const std::string& token) {
    if (token.size() < 3 || token.size() > 32) return false;
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (token.front() == '_' || token.find("__") == 0) return false;
    if (token.find('/') != std::string::npos) return false;
    const std::vector<std::string> blocked = {
        "afl", "cmplog", "forkserver", "shmat", "shmem", "map_size",
        "debug:", "fs_error", "libsystem", "dyld", "asan", "ubsan",
        "sanitizer", "waitpid", "could not"};
    for (const auto& item : blocked) {
      if (lower.find(item) != std::string::npos) {
        return false;
      }
    }
    return std::all_of(token.begin(), token.end(), [](unsigned char c) {
      return c >= 0x20 && c <= 0x7e;
    });
  };

  // Lightweight string array parsing. The extractor has used both the old
  // extracted_strings key and the newer magic_tokens key across M4 iterations.
  auto extract_array = [&](const std::string& key) -> std::vector<std::string> {
      std::vector<std::string> results;
      auto pos = static_json.find("\"" + key + "\"");
      if (pos == std::string::npos) return results;
      auto start = static_json.find('[', pos);
      auto end = static_json.find(']', start);
      if (start == std::string::npos || end == std::string::npos) return results;

      std::string slice = static_json.substr(start + 1, end - start - 1);
      std::size_t s_pos = 0;
      while (s_pos < slice.size()) {
          auto q1 = slice.find('"', s_pos);
          if (q1 == std::string::npos) break;
          auto q2 = q1 + 1;
          // Handle escaped quotes
          while (q2 < slice.size() && (slice[q2] != '"' || slice[q2-1] == '\\')) q2++;
          if (q2 >= slice.size()) break;
          results.push_back(decode_json_string(slice.substr(q1 + 1, q2 - q1 - 1)));
          s_pos = q2 + 1;
      }
      return results;
  };

  auto strings = extract_array("magic_tokens");
  auto legacy_strings = extract_array("extracted_strings");
  strings.insert(strings.end(), legacy_strings.begin(), legacy_strings.end());

  std::set<std::string> seen_tokens;
  int token_count = 0;
  for (const auto& token : strings) {
    if (token_count >= 200) break;
    if (!should_keep_token(token)) continue;

    const auto escaped = afl_dict_escape(token);
    if (escaped.empty() || !seen_tokens.insert(escaped).second) continue;

    dict_out << "static_" << token_count << "=\"" << escaped << "\"\n";
    ++token_count;
  }

  auto process_cmp_constants = [&](const std::string& key) {
    auto pos = static_json.find("\"" + key + "\"");
    if (pos == std::string::npos) return;
    auto start = static_json.find('[', pos);
    auto end = static_json.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return;

    std::string slice = static_json.substr(start + 1, end - start - 1);
    std::stringstream ss(slice);
    std::string val_str;
    while (std::getline(ss, val_str, ',') && token_count < 250) {
      val_str = trim(val_str);
      if (val_str.size() >= 2 && val_str.front() == '"' && val_str.back() == '"') {
        val_str = decode_json_string(val_str.substr(1, val_str.size() - 2));
      }
      if (val_str.empty()) continue;
      try {
        const uint64_t v = std::stoull(val_str);
        if (v < 0x20202020 || v > 0x7E7E7E7E) continue;

        for (const bool little_endian : {true, false}) {
          std::string decoded;
          for (int i = 0; i < 4; ++i) {
            const int shift = little_endian ? i * 8 : (3 - i) * 8;
            decoded.push_back(static_cast<char>((v >> shift) & 0xff));
          }
          if (!should_keep_token(decoded)) continue;

          const auto escaped = afl_dict_escape(decoded);
          if (escaped.empty() || !seen_tokens.insert(escaped).second) continue;

          dict_out << "static_cmp_" << token_count++ << "=\"" << escaped << "\"\n";
          if (token_count >= 250) break;
        }
      } catch (...) {
      }
    }
  };

  process_cmp_constants("cmp_constants");
  if (token_count < 250) {
    process_cmp_constants("extracted_cmp_consts");
  }

  return dict_path;
}

std::string coverage_csv_row(const AflStats& stats) {
  return std::to_string(stats.sampled_at) + "," +
         std::to_string(stats.execs_done) + "," +
         std::to_string(stats.execs_per_sec) + "," +
         std::to_string(stats.paths_total) + "," +
         std::to_string(stats.edges_found) + "," +
         std::to_string(stats.bitmap_cvg) + "," +
         std::to_string(stats.unique_crashes) + "," +
         std::to_string(stats.unique_hangs) + "," +
         std::to_string(stats.recipe_hits) + "," +
         std::to_string(stats.recipe_misses);
}

AflStats read_stats_or_throw(const std::filesystem::path& path) {
  std::string error;
  auto stats = parse_fuzzer_stats(path, &error);
  if (!stats) {
    throw std::runtime_error(error);
  }
  return *stats;
}

ProcessStatus stop_afl_process(int pid, int graceful_timeout_ms) {
  ProcessStatus status;
  if (pid <= 0) {
    return status;
  }
  // Send graceful SIGINT first — AFL++ catches it and writes out
  // fuzzer_stats one last time before exiting cleanly.
  (void)interrupt_process(pid);
  status = wait_process(pid, graceful_timeout_ms);
  if (status.exited || status.signaled) {
    return status;
  }
  // Escalate to SIGTERM, this time on the entire process group so any
  // AFL++ worker children are also reaped. Without the -pid group send
  // we used to leave orphaned forkserver children on hard shutdowns.
  (void)terminate_process_group(pid);
  status = wait_process(pid, 2000);
  if (status.exited || status.signaled) {
    return status;
  }
  // Last resort SIGKILL to the group.
  (void)kill_process_group(pid);
  return wait_process(pid, 1000);
}

std::string normalize_provider_name(std::string provider) {
  for (char& c : provider) {
    if (c == '_') {
      c = '-';
    } else {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return provider;
}

std::string normalize_ablation_mode(std::string mode) {
  if (mode.empty()) {
    return "full-agent";
  }
  for (char& c : mode) {
    if (c == '_') {
      c = '-';
    } else {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return mode;
}

void apply_run_overrides(AppConfig& config, RunOptions& options) {
  options.ablation_mode = normalize_ablation_mode(options.ablation_mode);
  if (options.main_budget_override_sec > 0) {
    config.afl.main_budget_sec = options.main_budget_override_sec;
  }
  if (options.micro_budget_override_sec > 0) {
    config.micro_campaign.budget_sec = options.micro_budget_override_sec;
  }
  if (options.plateau_window_override_sec > 0) {
    config.afl.plateau_window_sec = options.plateau_window_override_sec;
  }

  if (options.ablation_mode == "full") {
    options.ablation_mode = "full-agent";
  } else if (options.ablation_mode == "baseline") {
    options.ablation_mode = "baseline-afl";
  } else if (options.ablation_mode == "controller-only") {
    // controller-only is the paper-side name for the ablation in which
    // the FuzzPilot controller (plateau detector + agent runtime +
    // micro-campaign + LLM proposals) is fully active but the
    // recipe-guided custom mutator is disabled so AFL++ falls back to
    // its native havoc stage. That is mechanistically identical to the
    // no-mutator arm; we alias here so the manifest can carry the
    // paper terminology without diverging from the implementation.
    options.ablation_mode = "no-mutator";
  }

  if (options.ablation_mode == "baseline-afl") {
    config.micro_campaign.enabled = false;
    config.mutation_strategy.enabled = false;
    config.static_analysis.enabled = false;
    config.model_api.enabled = false;
    options.model_provider = "fake";
  } else if (options.ablation_mode == "rule-only") {
    config.model_api.enabled = false;
    options.model_provider = "fake";
  } else if (options.ablation_mode == "no-static-analysis") {
    config.static_analysis.enabled = false;
    options.disable_static_analysis = true;
  } else if (options.ablation_mode == "no-mutator") {
    config.mutation_strategy.enabled = false;
  } else if (options.ablation_mode == "no-microcampaign") {
    // Skip the micro-campaign validation step entirely — top-1 agent
    // proposal is promoted directly. Measures the value of the validation
    // closed loop.
    config.micro_campaign.enabled = false;
    options.disable_microcampaign = true;
  } else if (options.ablation_mode == "no-plateau") {
    // Run agents on a fixed cadence (or never) regardless of coverage
    // growth. Measures the value of plateau-triggered intervention.
    options.disable_plateau_detector = true;
  } else if (options.ablation_mode == "random-recipe") {
    // Recipes drawn from a uniform random distribution over operators.
    options.recipe_source = "random";
  } else if (options.ablation_mode == "random-reward") {
    options.reward_mode = "random";
  } else if (options.ablation_mode == "edges-only") {
    options.reward_mode = "edges_only";
  } else if (options.ablation_mode == "ai-direct") {
    options.direct_promote_without_microcampaign = true;
  } else if (options.ablation_mode == "single-agent-coordinator") {
    options.disabled_agents = {
        "PlateauDiagnosisAgent", "SchedulerAgent", "CmpAgent", "MutatorAgent",
        "DictionaryAgent", "FormatAgent", "CorpusAgent"};
  } else if (options.ablation_mode == "single-agent-dictionary") {
    options.disabled_agents = {
        "CoordinatorAgent", "PlateauDiagnosisAgent", "SchedulerAgent", "CmpAgent",
        "MutatorAgent", "FormatAgent", "CorpusAgent"};
  } else if (options.ablation_mode == "no-semantic-context") {
    options.suppress_semantic_context = true;
    config.static_analysis.enabled = false;
    options.disable_static_analysis = true;
  } else if (options.ablation_mode != "full-agent") {
    throw std::runtime_error("unsupported ablation mode: " + options.ablation_mode);
  }
}

void configure_agent_tasks(std::vector<AgentTask>& tasks, const AppConfig& config,
                           const RunOptions& options) {
  for (auto& task : tasks) {
    task.timeout_ms = static_cast<uint32_t>(std::max(1000, config.agent_runtime.per_agent_timeout_ms));
    task.max_output_tokens = static_cast<uint32_t>(std::max(256, config.model_api.max_output_tokens));
  }
  tasks = filter_disabled_agents(std::move(tasks), options.disabled_agents);
}

uint64_t agent_block_deadline_unix_sec(const std::vector<AgentTask>& tasks,
                                       uint64_t max_budget_sec = 1800) {
  uint64_t budget_sec = 5;
  for (const auto& task : tasks) {
    budget_sec += std::max<uint64_t>(1, (static_cast<uint64_t>(task.timeout_ms) + 999) / 1000);
    budget_sec += 1;  // run_agent_tasks staggers provider calls.
  }
  budget_sec = std::max<uint64_t>(60, budget_sec);
  if (max_budget_sec > 0) {
    budget_sec = std::min(budget_sec, max_budget_sec);
  }
  return static_cast<uint64_t>(std::time(nullptr)) + budget_sec;
}

std::string env_or_fallback(const std::string& env_name, const std::string& fallback) {
  if (!env_name.empty()) {
    if (const char* value = std::getenv(env_name.c_str()); value != nullptr && value[0] != '\0') {
      return value;
    }
  }
  return fallback;
}

std::unique_ptr<IModelGateway> make_gateway(const RunOptions& options, const AppConfig& config) {
  std::string provider;
  if (!options.model_provider.empty()) {
    provider = options.model_provider;
  } else if (!config.model_api.enabled) {
    provider = "fake";
  } else {
    provider = config.model_api.provider;
  }

  provider = normalize_provider_name(provider);
  if (provider.empty() || provider == "fake") {
    return std::make_unique<FakeModelGateway>();
  }

  if (provider == "openai-compatible") {
    const auto endpoint = options.model_endpoint.empty()
                              ? env_or_fallback(config.model_api.endpoint_env,
                                                config.model_api.endpoint)
                              : options.model_endpoint;
    const auto model_name = options.model_name.empty() ? config.model_api.model
                                                       : options.model_name;
    const auto api_key_env = options.api_key_env.empty() ? config.model_api.api_key_env
                                                         : options.api_key_env;
    if (endpoint.empty()) {
      throw std::runtime_error("missing model endpoint for openai-compatible provider");
    }
    if (model_name.empty()) {
      throw std::runtime_error("missing model name for openai-compatible provider");
    }
    if (api_key_env.empty()) {
      throw std::runtime_error("missing api_key_env for openai-compatible provider");
    }
    auto lowercase = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return value;
    };
    const auto lower_endpoint = lowercase(endpoint);
    const auto lower_model = lowercase(model_name);
    const bool glm_endpoint = lower_endpoint.find("open.bigmodel.cn") != std::string::npos ||
                              lower_endpoint.find("bigmodel.cn/api/paas") != std::string::npos;
    const bool glm_model = lower_model.rfind("glm-", 0) == 0;
    if ((glm_endpoint || glm_model) && model_name != "glm-4-flash") {
      throw std::runtime_error("GLM provider is restricted to model glm-4-flash");
    }
    return std::make_unique<OpenAICompatibleGateway>(endpoint, model_name, api_key_env, true);
  }

  throw std::runtime_error("unsupported model provider: " + provider);
}

void persist_agent_memory(Database& db,
                          const RunSummary& summary,
                          const AppConfig& config,
                          const AgentDecision& decision,
                          double reward_hint) {
  const auto now = static_cast<uint64_t>(std::time(nullptr));

  std::string memory_val = decision.proposal_json;
  if (const auto patch = extract_top_level_json_value(decision.proposal_json, "memory_patch")) {
    memory_val = *patch;
  }

  const auto key = summary.plateau_id + ":" + decision.agent + ":" +
                   decision.model_response.response_hash;
  const auto evidence = std::string("{\"decision_id\":\"") + json_escape(decision.id) +
                        "\",\"context_hash\":\"" +
                        json_escape(decision.model_response.context_hash) +
                        "\",\"schema_valid\":" +
                        (decision.model_response.schema_valid ? "true" : "false") + "}";
  db.insert_agent_memory(make_id("memory"), summary.run_id, config.target.name, decision.agent,
                         "proposal_memory_patch", key, memory_val, evidence,
                         reward_hint, decision.model_response.schema_valid ? 0.7 : 0.2, now);
  append_line(summary.agent_memory_path,
              std::string("{\"run_id\":\"") + json_escape(summary.run_id) +
                  "\",\"agent\":\"" + json_escape(decision.agent) +
                  "\",\"key\":\"" + json_escape(key) +
                  "\",\"reward_hint\":" + std::to_string(reward_hint) +
                  ",\"memory\":" +
                  json_value_or_raw(memory_val) + "}");
}

void persist_agent_decision(Database& db,
                            RunSummary& summary,
                            const AppConfig& config,
                            const AgentDecision& decision,
                            double reward_hint,
                            const std::filesystem::path& events_path) {
  db.insert_agent_decision(decision);
  ++summary.agent_decision_count;
  // Accumulate LLM accounting for end-of-run aggregation. error_kind
  // != "ok" counts as a failed call; tokens/latency are still added
  // because we paid for them.
  ++summary.llm_calls;
  if (decision.model_response.error_kind != "ok") {
    ++summary.llm_failed_calls;
  }
  summary.llm_input_tokens += decision.model_response.input_tokens;
  summary.llm_output_tokens += decision.model_response.output_tokens;
  summary.llm_total_latency_ms += static_cast<double>(decision.model_response.latency_ms);
  append_line(events_path, std::string("{\"event\":\"agent_decision\",\"decision\":") +
                               agent_decision_json(decision) + "}");
  append_line(summary.agent_replay_log_path, agent_decision_json(decision));
  persist_agent_memory(db, summary, config, decision, reward_hint);
}

std::string micro_results_json(const std::vector<MicroResult>& results) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < results.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << micro_result_json(results[i]);
  }
  out << "]";
  return out.str();
}

SeedMutationStrategy build_promoted_strategy(const RunOptions& options,
                                             const std::string& run_id,
                                             const std::string& winner_id,
                                             const std::string& agent,
                                             std::vector<std::string> tokens) {
  if (tokens.empty()) {
    tokens = {"PROMOTED", winner_id, "FUZZ", "TOKEN"};
  } else {
    tokens.insert(tokens.begin(), winner_id);
    tokens.insert(tokens.begin(), "PROMOTED");
  }

  SeedMutationStrategy promoted;
  if (options.recipe_source == "random") {
    const uint64_t seed_value = std::hash<std::string>{}(run_id);
    promoted = make_random_recipe_strategy(seed_value, tokens);
  } else {
    promoted = make_default_dictionary_strategy(std::move(tokens));
    if (!agent.empty()) {
      promoted.agent = agent;
    }
  }
  promoted.id = make_id("strategy_promoted");
  return promoted;
}

std::filesystem::path write_promoted_recipe_index(const std::filesystem::path& recipe_store_dir,
                                                  const RunOptions& options,
                                                  const std::string& run_id,
                                                  const std::string& winner_id,
                                                  const std::string& agent,
                                                  std::vector<std::string> tokens) {
  RecipeStore promoted_store(recipe_store_dir);
  const auto index_path = promoted_store.write_compact_recipes({
      build_promoted_strategy(options, run_id, winner_id, agent, std::move(tokens))});
  const auto global_path = recipe_store_dir / "global.recipe";
  if (std::filesystem::exists(global_path)) {
    return global_path;
  }
  return index_path;
}

void write_report(const RunSummary& summary,
                  const AppConfig& config,
                  const std::vector<AflStats>& main_samples,
                  const std::vector<MicroResult>& micro_results,
                  const std::vector<AgentDecision>& decisions) {
  std::filesystem::create_directories(summary.report_path.parent_path());
  std::ofstream report(summary.report_path);
  if (!report) {
    throw std::runtime_error("failed to write report: " + summary.report_path.string());
  }
  report << "# FuzzPilot MVP Run Report\n\n";
  report << "Run ID: `" << summary.run_id << "`\n\n";
  report << "Project: `" << config.project << "`\n\n";
  report << "Target: `" << config.target.name << "`\n\n";
  report << "Ablation mode: `" << summary.ablation_mode << "`\n\n";
  report << "Plateau ID: `" << summary.plateau_id << "`\n\n";
  report << "Main AFL launch plan: `" << summary.main_launch_path.string() << "`\n\n";
  report << "Main AFL PID: `" << summary.main_pid << "`\n\n";
  report << "Coverage CSV: `" << summary.coverage_csv_path.string() << "`\n\n";
  report << "Agent replay log: `" << summary.agent_replay_log_path.string() << "`\n\n";
  report << "Agent memory: `" << summary.agent_memory_path.string() << "`\n\n";
  report << "Winner intervention: `" << summary.winner_intervention_id << "`\n\n";
  report << "Winner reward: `" << summary.winner_reward << "`\n\n";
  report << "Promoted recipe index: `" << summary.promoted_recipe_index.string() << "`\n\n";
  report << "## Coverage\n\n";
  if (!main_samples.empty()) {
    const auto& first = main_samples.front();
    const auto& last = main_samples.back();
    report << "- samples=`" << main_samples.size() << "` execs_delta=`"
           << (last.execs_done > first.execs_done ? last.execs_done - first.execs_done : 0)
           << "` paths_delta=`"
           << (last.paths_total > first.paths_total ? last.paths_total - first.paths_total : 0)
           << "` crashes=`" << last.unique_crashes << "`\n\n";
  }
  report << "## Agent Decisions\n\n";
  if (decisions.empty()) {
    report << "- none\n";
  }
  for (const auto& decision : decisions) {
    report << "- `" << decision.agent << "` provider=`" << decision.model_response.provider
           << "` schema_valid=`" << (decision.model_response.schema_valid ? "true" : "false")
           << "` context=`" << decision.model_response.context_hash << "`\n";
  }
  report << "\n## Micro Results\n\n";
  if (micro_results.empty()) {
    report << "- none\n";
  }
  for (const auto& result : micro_results) {
    report << "- `" << result.intervention_id << "` campaign=`" << result.campaign_id
           << "` reward=`" << result.reward << "` new_paths=`" << result.new_paths
           << "` promoted=`" << (result.promoted ? "true" : "false") << "`\n";
  }
}

}  // namespace

std::optional<AgentDecision> select_direct_promotion_for_test(
    const std::vector<AgentDecision>& decisions) {
  for (const auto& decision : decisions) {
    if (!decision.model_response.schema_valid) {
      continue;
    }
    if (decision.proposal_json.find("\"interventions\"") == std::string::npos) {
      continue;
    }
    if (decision.proposal_json.find("\"default_control\"") != std::string::npos) {
      continue;
    }
    return decision;
  }
  return std::nullopt;
}

RunSummary run_mvp(const RunOptions& requested_options) {
  RunOptions options = requested_options;
  if (options.config_path.empty()) {
    throw std::runtime_error("RunOptions.config_path is required");
  }
  const auto loaded = load_config(options.config_path);
  auto config = loaded.config;
  apply_run_overrides(config, options);

  RunSummary summary;
  summary.run_id = make_id("run");
  summary.main_campaign_id = "main_" + summary.run_id;
  summary.ablation_mode = options.ablation_mode;
  summary.run_dir = options.work_dir / summary.run_id;
  summary.db_path = options.db_path.empty() ? (summary.run_dir / "fuzzpilot.sqlite") : options.db_path;
  summary.report_path = summary.run_dir / "report.md";
  summary.coverage_csv_path = summary.run_dir / "coverage.csv";
  summary.agent_replay_log_path = summary.run_dir / "agent_decisions.jsonl";
  summary.agent_memory_path = summary.run_dir / "agent_memory.jsonl";
  summary.main_launch_path = summary.run_dir / "main_launch.sh";

  std::filesystem::create_directories(summary.run_dir);
  const auto events_path = summary.run_dir / "events.jsonl";
  const auto main_output_dir = summary.run_dir / "main_out";
  auto active_main_output_dir = main_output_dir;
  const auto main_recipe_store = summary.run_dir / "main_recipes";
  const auto now = static_cast<uint64_t>(std::time(nullptr));

  Database db;
  db.open(summary.db_path);
  db.initialize_schema(options.schema_path);
  const auto env = capture_env_snapshot(config.afl.afl_fuzz.string());
  db.insert_run(summary.run_id, config.project, config.target.name, now, "running",
                env.os, env.arch, env.afl_version, options.ablation_mode);
  db.insert_campaign(summary.main_campaign_id, summary.run_id, "main", "", "",
                     main_output_dir, now,
                     static_cast<uint64_t>(config.afl.main_budget_sec), "running");

  if (config.mutation_strategy.enabled) {
    RecipeStore main_store(main_recipe_store);
    auto initial_tokens = load_tokens_from_dict(config.target.dict);
    if (initial_tokens.empty()) {
      initial_tokens = {"FUZZ", "MAGIC", "TOKEN"};
    }
    main_store.write_compact_recipes({make_default_dictionary_strategy(initial_tokens)});
  }
  const auto main_launch = build_main_afl_spec(config, main_output_dir, main_recipe_store);
  write_text_file(summary.main_launch_path,
                  "#!/usr/bin/env sh\n" + shell_preview(main_launch) + "\n");
  append_line(events_path, std::string("{\"event\":\"main_afl_plan\",\"command\":\"") +
                               json_escape(shell_preview(main_launch)) + "\"}");
  if (!options.dry_run) {
    const auto process = spawn_process(main_launch.afl_fuzz.string(),
                                       main_launch.argv,
                                       main_launch.env);
    if (process.pid < 0) {
      throw std::runtime_error("failed to launch AFL++ main campaign: " + process.error);
    }
    summary.main_pid = process.pid;
    append_line(events_path, std::string("{\"event\":\"main_afl_launched\",\"pid\":") +
                                 std::to_string(summary.main_pid) + "}");
  }

  PlateauConfig plateau_config;
  plateau_config.window_sec = static_cast<uint64_t>(config.afl.plateau_window_sec);
  plateau_config.max_new_paths = static_cast<uint64_t>(std::max(0, config.afl.plateau_min_new_edges));
  plateau_config.min_execs_delta = 1000;
  plateau_config.disabled = options.disable_plateau_detector;
  // Scale minimum sample count to window: aim for at least one sample per
  // 30 seconds in the window, with a hard floor of 5 (for short test
  // windows used in CI).
  plateau_config.min_samples = std::max<std::size_t>(
      5, static_cast<std::size_t>(plateau_config.window_sec / 30));
  PlateauDetector detector(plateau_config);

  std::vector<AflStats> main_samples;
  write_text_file(summary.coverage_csv_path,
                  "ts,execs_done,execs_per_sec,paths_total,edges_found,bitmap_cvg,"
                  "unique_crashes,unique_hangs,recipe_hits,recipe_misses\n");

  // Reward tracker for in-context RL: every LLM decision is "deployed" at
  // its emit time; we credit subsequent edge-growth back to recent decisions
  // within a 600s window and feed top/bottom-k back into the next prompt.
  RecipeRewardTracker reward_tracker(summary.run_dir / "recipe_rewards.jsonl");

  auto process_stats = [&](const AflStats& stats) {
    main_samples.push_back(stats);
    db.insert_telemetry(summary.main_campaign_id, stats);
    ++summary.telemetry_count;
    append_line(summary.coverage_csv_path, coverage_csv_row(stats));
    append_line(events_path, telemetry_event_json(summary.run_id, summary.main_campaign_id, stats));
    // Feed edge-growth into the reward tracker so prior decisions get
    // credited over the credit window.
    reward_tracker.observe_edges(stats.edges_found,
                                 static_cast<uint64_t>(stats.sampled_at));
    const auto plateau = detector.add_sample(stats, summary.run_id, summary.main_campaign_id);
    if (plateau && summary.plateau_id.empty()) {
      summary.plateau_id = plateau->id;
      const auto blackboard = plateau_blackboard_json(*plateau, stats);
      db.insert_plateau(*plateau, blackboard);
      append_line(events_path, std::string("{\"event\":\"plateau_detected\",\"plateau\":") +
                                   plateau_event_json(*plateau) + "}");
    }
  };

  // --- Initial binary intelligence scan via the configured reverse-engineering backend. ---
  std::string base_intelligence_json = "{}";
  if (config.static_analysis.enabled) {
    const auto intel_path = summary.run_dir / "base_intelligence.json";
    if (!config.static_analysis.context_path.empty()) {
      base_intelligence_json =
          read_precomputed_static_context(config.static_analysis.context_path);
      write_text_file(intel_path, base_intelligence_json);
      append_line(events_path,
                  std::string("{\"event\":\"static_context_precomputed_loaded\","
                              "\"path\":\"") +
                      json_escape(config.static_analysis.context_path.string()) +
                      "\",\"context_size\":" +
                      std::to_string(base_intelligence_json.size()) + "}");
    } else if (!std::filesystem::exists(intel_path)) {
      append_line(events_path,
                  std::string("{\"event\":\"static_initial_scan_started\",\"backend\":\"") +
                      json_escape(normalize_static_backend(config.static_analysis.backend)) + "\"}");
      base_intelligence_json = run_static_extractor(config.static_analysis,
                                                    std::filesystem::absolute(config.target.binary),
                                                    summary.run_dir);

      const auto raw_static_path = summary.run_dir / "static_context.json";
      if (std::filesystem::exists(raw_static_path)) {
        std::filesystem::rename(raw_static_path, intel_path);
      } else {
        write_text_file(intel_path, base_intelligence_json);
      }
      append_line(events_path, "{\"event\":\"static_initial_scan_done\"}");
    } else {
      std::ifstream ifs(intel_path);
      std::ostringstream ss;
      ss << ifs.rdbuf();
      base_intelligence_json = ss.str();
    }
  }

  // Helper: build a prose few-shot block for the next agent prompt. Uses
  // up to 5 GOOD + 5 BAD examples; empty when no decisions have any credit.
  auto format_few_shot_block = [&]() -> std::string {
    const auto top = reward_tracker.topk(5);
    const auto bottom = reward_tracker.bottomk(5);
    bool any_credit = false;
    for (const auto& e : top) {
      if (e.apply_count > 0) { any_credit = true; break; }
    }
    if (!any_credit) return "";
    std::ostringstream out;
    out << "## Prior decisions and observed effectiveness\n"
        << "The following are previous proposals from this run with the "
           "edge-growth credit they earned over the 10 minutes after deployment. "
           "Prefer the GOOD patterns and avoid the BAD ones in your next proposal.\n";
    if (!top.empty()) {
      out << "\nGOOD (high reward):\n";
      for (const auto& e : top) {
        if (e.apply_count == 0) continue;
        out << "- agent=" << e.agent_name
            << " reward=" << e.reward
            << " summary=" << e.summary << "\n";
      }
    }
    if (!bottom.empty()) {
      out << "\nBAD (low or zero reward):\n";
      for (const auto& e : bottom) {
        out << "- agent=" << e.agent_name
            << " reward=" << e.reward
            << " summary=" << e.summary << "\n";
      }
    }
    return out.str();
  };

  // Helper: condense a proposal_json to a short prose summary suitable for
  // future few-shot prompts. We just truncate-and-escape; the LLM is fine
  // with raw JSON snippets and it keeps prompt size bounded.
  auto short_proposal_summary = [&](const std::string& proposal_json) {
    constexpr std::size_t kMax = 200;
    if (proposal_json.size() <= kMax) return proposal_json;
    return proposal_json.substr(0, kMax) + "...";
  };

  // Construct the model gateway up front so plateau-triggered agent
  // interventions inside the main loop can reuse it. Failing early is
  // preferable to running AFL for 24h and silently never calling the LLM.
  auto inline_gateway = make_gateway(options, config);
  const bool agent_inline_enabled =
      options.ablation_mode != "baseline-afl" &&
      config.micro_campaign.enabled;
  std::size_t inline_intervention_count = 0;
  // Wall-clock seconds at which the inline agent last fired (natural
  // plateau OR heartbeat). Drives the heartbeat fallback so high-yield
  // targets like libxml2 don't run 24h without invoking the agent.
  int last_agent_trigger_at_sec = 0;
  // Helper to merge new seeds from a micro campaign queue back to the main fuzzer queue
  auto merge_micro_queue_to_main = [](const std::filesystem::path& micro_queue_dir,
                                       const std::filesystem::path& main_queue_dir,
                                       const std::string& prefix) {
    if (!std::filesystem::exists(micro_queue_dir) || !std::filesystem::is_directory(micro_queue_dir)) {
      return;
    }
    if (!std::filesystem::exists(main_queue_dir) || !std::filesystem::is_directory(main_queue_dir)) {
      return;
    }

    std::cerr << "[DEBUG] Merging micro queue " << micro_queue_dir << " to main queue " << main_queue_dir << "\n";
    std::size_t copied_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(micro_queue_dir)) {
      if (entry.is_regular_file()) {
        const auto filename = entry.path().filename().string();
        // Skip AFL++ metadata files and README
        if (filename.rfind(".", 0) == 0 || filename == "README.txt") {
          continue;
        }
        // Prefix to prevent collisions
        const auto target_filename = prefix + "_" + filename;
        const auto target_path = main_queue_dir / target_filename;
        try {
          std::filesystem::copy_file(entry.path(), target_path, std::filesystem::copy_options::overwrite_existing);
          copied_count++;
        } catch (const std::exception& ex) {
          std::cerr << "[DEBUG] Failed to copy seed " << filename << ": " << ex.what() << "\n";
        }
      }
    }
    std::cerr << "[DEBUG] Successfully merged " << copied_count << " seeds from micro campaign queue.\n";
  };

  std::vector<MicroResult> layered_micro_results;
  std::string early_exit_reason;
  bool received_term_signal = false;
  TelemetryCollector collector(active_main_output_dir, "");

  auto trigger_inline_agent = [&](const PlateauEvent& plateau,
                                   const AflStats& stats) {
    const auto recent = db.get_recent_decisions(summary.run_id, 10);
    const auto mem = db.get_agent_memory(summary.run_id);
    const auto bb = plateau_blackboard_json(plateau, stats,
                                            options.suppress_semantic_context ? "{}" : base_intelligence_json,
                                            recent, mem);
    auto tasks = make_default_agent_tasks(
        plateau.id, bb,
        static_cast<uint32_t>(config.micro_campaign.budget_sec),
        format_few_shot_block());
    configure_agent_tasks(tasks, config, options);
    const auto deadline = agent_block_deadline_unix_sec(tasks);
    auto decisions = run_agent_tasks(*inline_gateway, summary.run_id,
                                     plateau.id, tasks, deadline);
    for (const auto& d : decisions) {
      persist_agent_decision(db, summary, config, d, 0.0, events_path);
      reward_tracker.record_deploy(
          d.id, d.agent, short_proposal_summary(d.proposal_json),
          static_cast<uint64_t>(d.created_ts));
    }
    std::vector<std::string> new_tokens;
    for (const auto& d : decisions) {
      auto tokens = extract_dictionary_tokens_from_proposal(d.proposal_json);
      new_tokens.insert(new_tokens.end(), tokens.begin(), tokens.end());
    }

    // Create and execute micro campaigns from agent decisions
    if (options.direct_promote_without_microcampaign) {
      const auto direct = select_direct_promotion_for_test(decisions);
      if (direct) {
        const auto direct_tokens =
            extract_dictionary_tokens_from_proposal(direct->proposal_json);
        summary.winner_status = WinnerStatus::kSelected;
        summary.winner_intervention_id = direct->id;
        summary.winner_campaign_id.clear();
        summary.promoted_recipe_index = write_promoted_recipe_index(
            main_recipe_store, options, summary.run_id, direct->id,
            direct->agent, direct_tokens);
        append_line(events_path,
                    std::string("{\"event\":\"ai_direct_promoted\",\"ts\":") +
                        std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                        ",\"decision_count\":" + std::to_string(decisions.size()) +
                        ",\"source\":\"inline_agent\","
                        "\"decision_id\":\"" + json_escape(direct->id) +
                        "\",\"agent\":\"" + json_escape(direct->agent) +
                        "\",\"token_count\":" +
                        std::to_string(direct_tokens.size()) +
                        ",\"recipe_index\":\"" +
                        json_escape(summary.promoted_recipe_index.string()) + "\"}");
      } else {
        append_line(events_path,
                    std::string("{\"event\":\"ai_direct_no_candidate\",\"ts\":") +
                        std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                        ",\"decision_count\":" +
                        std::to_string(decisions.size()) +
                        ",\"source\":\"inline_agent\"}");
      }
    }

    if (config.micro_campaign.enabled && !decisions.empty() &&
        !options.direct_promote_without_microcampaign) {
      std::cerr << "[DEBUG] Creating micro campaigns from " << decisions.size() << " agent decisions\n";
      // Take a fresh corpus snapshot for this inline intervention.
      // Each inline intervention gets its own numbered snapshot so they
      // don't overwrite each other.
      const auto snapshot_dir = summary.run_dir /
          ("corpus_snapshot_inline_" + std::to_string(inline_intervention_count));
      const auto micro_dir = summary.run_dir / ("micro_inline_" + std::to_string(inline_intervention_count));

      // Snapshot the current AFL output corpus
      bool snapshot_ok = false;
      if (!options.dry_run) {
        try {
          snapshot_corpus(active_main_output_dir, snapshot_dir);
          snapshot_ok = true;
          append_line(events_path,
                      std::string("{\"event\":\"inline_corpus_snapshot\",\"snapshot_dir\":\"") +
                          json_escape(snapshot_dir.string()) + "\"}");
        } catch (const std::exception& ex) {
          std::cerr << "[DEBUG] Inline corpus snapshot failed: " << ex.what() << "\n";
          append_line(events_path,
                      std::string("{\"event\":\"inline_corpus_snapshot_failed\",\"error\":\"") +
                          json_escape(ex.what()) + "\"}");
        }
      } else {
        // Dry-run: use a pre-existing snapshot or input dir
        snapshot_ok = true;
      }

      if (snapshot_ok) {
        const auto snapshot_input = options.dry_run ? config.target.input_dir : snapshot_dir;
        const auto specs = plan_micro_campaigns(
            config, plateau.id, snapshot_input, micro_dir,
            options.dry_run, &db, summary.run_id);

        std::cerr << "[DEBUG] plan_micro_campaigns returned " << specs.size() << " specs\n";

        if (!specs.empty()) {
          std::cerr << "[DEBUG] Entering if (!specs.empty()) block\n";
          prepare_micro_campaigns(specs, new_tokens);
          std::cerr << "[DEBUG] prepare_micro_campaigns completed\n";
          append_line(events_path,
                      std::string("{\"event\":\"inline_micro_campaigns_created\",\"count\":") +
                          std::to_string(specs.size()) + "}");

          // Execute one full control -> dictionary -> seed-focus ->
          // per-seed spiral inline. plan_micro_campaigns() coalesces
          // agent proposals to at most these four stages, so this gives
          // each heartbeat a complete micro ladder without reopening the
          // old "one campaign per agent proposal" budget blow-up.
          const size_t max_inline_micros = std::min(size_t(4), specs.size());
          std::cerr << "[DEBUG] Executing top " << max_inline_micros << " micro campaigns inline\n";
          std::cerr << "[DEBUG] options.dry_run=" << options.dry_run << ", max_inline_micros=" << max_inline_micros << "\n";

          if (!options.dry_run && max_inline_micros > 0) {
            // Stop main AFL temporarily
            if (summary.main_pid > 0) {
              std::cerr << "[DEBUG] Stopping main AFL (pid=" << summary.main_pid << ") for inline micro campaigns\n";
              const auto stop_status = stop_afl_process(summary.main_pid, 5000);
              append_line(events_path,
                          std::string("{\"event\":\"main_afl_stopped_for_inline_micro\",\"pid\":") +
                              std::to_string(summary.main_pid) +
                              ",\"exited\":" + (stop_status.exited ? "true" : "false") + "}");
              summary.main_pid = -1;
            }

            std::filesystem::path previous_spiral_queue;

            // Execute the spiral stages in order. Each successful stage's
            // queue becomes the next stage's input corpus, so dictionary
            // probing can lift seed focus, and seed focus can lift per-seed
            // recipes instead of all stages competing from the same snapshot.
            for (size_t i = 0; i < max_inline_micros; ++i) {
              const auto& spec = specs[i];
              auto runtime_spec = spec;
              if (!previous_spiral_queue.empty() &&
                  std::filesystem::is_directory(previous_spiral_queue)) {
                runtime_spec.input_dir = previous_spiral_queue;
                append_line(events_path,
                            std::string("{\"event\":\"micro_spiral_input_selected\","
                                        "\"campaign_id\":\"") +
                                json_escape(runtime_spec.id) +
                                "\",\"input_dir\":\"" +
                                json_escape(runtime_spec.input_dir.string()) +
                                "\",\"depends_on_intervention_id\":\"" +
                                json_escape(runtime_spec.depends_on_intervention_id) +
                                "\"}");
              }
              const auto campaign_start_ts = static_cast<uint64_t>(std::time(nullptr));

              std::cerr << "[DEBUG] Launching inline micro campaign " << (i+1) << "/" << max_inline_micros
                        << " (intervention: " << runtime_spec.intervention_id << ")\n";

              db.insert_campaign(runtime_spec.id, summary.run_id, "micro_inline", summary.main_campaign_id,
                               runtime_spec.intervention_id, runtime_spec.output_dir, campaign_start_ts, runtime_spec.budget_sec, "running");

              // Use empty dict path for inline micro campaigns (they use the prepared recipes)
              const auto micro_launch = build_micro_afl_spec(config, runtime_spec, std::filesystem::path());
              write_text_file(runtime_spec.output_dir / "launch.sh", "#!/usr/bin/env sh\n" + shell_preview(micro_launch) + "\n");

              const auto process = spawn_process(micro_launch.afl_fuzz.string(), micro_launch.argv, micro_launch.env);

              if (process.pid > 0) {
                std::cerr << "[DEBUG] Micro campaign launched with pid=" << process.pid << "\n";
                append_line(events_path,
                            std::string("{\"event\":\"inline_micro_afl_launched\",\"pid\":") +
                                std::to_string(process.pid) + ",\"campaign_id\":\"" + runtime_spec.id + "\"}");

                // Wait for micro campaign to complete
                const int max_wait_ms = runtime_spec.budget_sec * 1000 + 5000;
                const auto wait_status = wait_process(process.pid, max_wait_ms);

                if (!wait_status.exited && !wait_status.signaled) {
                  std::cerr << "[DEBUG] Micro campaign timeout, killing pid=" << process.pid << "\n";
                  stop_afl_process(process.pid, 3000);
                }

                // Parse results
                std::string error;
                auto micro_stats = parse_fuzzer_stats(runtime_spec.output_dir / "default" / "fuzzer_stats", &error);

                const auto campaign_end_ts = static_cast<uint64_t>(std::time(nullptr));
                if (micro_stats) {
                  db.finish_campaign(runtime_spec.id, campaign_end_ts, "completed", "completed");
                  std::cerr << "[DEBUG] Micro campaign completed: edges=" << micro_stats->edges_found << "\n";

                  AflStats micro_parent_baseline;
                  auto result = evaluate_micro_result(runtime_spec.intervention_id, runtime_spec.id, micro_parent_baseline,
                                                      *micro_stats, reward_mode_from_string(options.reward_mode));
                  db.insert_micro_result(result);
                  layered_micro_results.push_back(result);
                  ++summary.micro_campaign_count;
                  append_line(events_path,
                              std::string("{\"event\":\"micro_layer_result\",\"layer_index\":") +
                                  std::to_string(inline_intervention_count) +
                                  ",\"agent\":\"" + json_escape(runtime_spec.agent) +
                                  "\",\"action\":\"" + json_escape(runtime_spec.name) +
                                  "\",\"source_decision_id\":\"" + json_escape(runtime_spec.source_decision_id) +
                                  "\",\"intervention_id\":\"" + json_escape(runtime_spec.intervention_id) +
                                  "\",\"campaign_id\":\"" + json_escape(runtime_spec.id) +
                                  "\",\"parent_edges\":" + std::to_string(stats.edges_found) +
                                  ",\"micro_edges\":" + std::to_string(micro_stats->edges_found) +
                                  ",\"new_edges\":" + std::to_string(result.new_edges) +
                                  ",\"new_paths\":" + std::to_string(result.new_paths) +
                                  ",\"reward\":" + std::to_string(result.reward) + "}");

                  // Dynamic feedback synchronization (Left foot stepping on right foot!)
                  const auto micro_queue = runtime_spec.output_dir / "default" / "queue";
                  const auto main_queue = active_main_output_dir / "default" / "queue";
                  const std::string prefix = "micro_r" + std::to_string(inline_intervention_count) + "_" + runtime_spec.id;
                  merge_micro_queue_to_main(micro_queue, main_queue, prefix);
                  if (std::filesystem::is_directory(micro_queue)) {
                    previous_spiral_queue = micro_queue;
                  }
                } else {
                  db.finish_campaign(runtime_spec.id, campaign_end_ts, "failed", "stats_unreadable");
                  std::cerr << "[DEBUG] Micro campaign failed: " << error << "\n";
                }
              } else {
                std::cerr << "[DEBUG] Failed to launch micro campaign: " << process.error << "\n";
                const auto fail_ts = static_cast<uint64_t>(std::time(nullptr));
                db.finish_campaign(runtime_spec.id, fail_ts, "failed", "spawn_failed");
              }
            }

            // Restart main AFL from a clean seed directory captured after
            // queue merge. AFL++'s in-place `-i -` resume is fragile after
            // controller-side queue edits and has aborted with `_resume
            // directory cleanup failed` on libxml2. A clean input dir keeps
            // the restart explicit and reproducible.
            std::filesystem::path restart_input_dir;
            try {
              restart_input_dir = summary.run_dir /
                  ("main_restart_input_" + std::to_string(inline_intervention_count));
              snapshot_corpus(active_main_output_dir, restart_input_dir);
              append_line(events_path,
                          std::string("{\"event\":\"main_afl_restart_input_prepared\","
                                      "\"input_dir\":\"") +
                              json_escape(restart_input_dir.string()) + "\"}");
            } catch (const std::exception& ex) {
              append_line(events_path,
                          std::string("{\"event\":\"main_afl_restart_input_failed\","
                                      "\"error\":\"") +
                              json_escape(ex.what()) + "\"}");
              restart_input_dir.clear();
            }

            // Restart main AFL
            std::cerr << "[DEBUG] Restarting main AFL after inline micro campaigns\n";
            const auto main_launch = build_main_restart_afl_spec(
                config, summary.run_dir, main_recipe_store,
                inline_intervention_count, restart_input_dir);
            const auto main_process = spawn_process(main_launch.afl_fuzz.string(), main_launch.argv, main_launch.env);

	            if (main_process.pid > 0) {
	              summary.main_pid = main_process.pid;
	              active_main_output_dir = main_launch.output_dir;
	              collector = TelemetryCollector(active_main_output_dir, "");
	              std::cerr << "[DEBUG] Main AFL restarted with pid=" << main_process.pid << "\n";
	              bool restart_exited = false;
	              ProcessStatus restart_status;
	              for (int poll = 0; poll < 20; ++poll) {
	                restart_status = wait_process(main_process.pid, 0);
	                if (restart_status.exited || restart_status.signaled) {
	                  restart_exited = true;
	                  break;
	                }
	                if (std::filesystem::exists(
	                        find_fuzzer_stats_file(active_main_output_dir))) {
	                  break;
	                }
	                std::this_thread::sleep_for(std::chrono::milliseconds(250));
	              }
	              if (restart_exited) {
	                std::ostringstream message;
	                message << "AFL++ main restart exited before fresh telemetry";
	                if (restart_status.exited) {
	                  message << " with exit_code=" << restart_status.exit_code;
	                }
	                if (restart_status.signaled) {
	                  message << " from signal=" << restart_status.term_signal;
	                }
	                early_exit_reason = message.str();
	                summary.main_pid = -1;
	                append_line(events_path,
	                            std::string("{\"event\":\"main_afl_restart_failed\","
	                                        "\"exit_code\":") +
	                                std::to_string(restart_status.exit_code) +
	                                ",\"signaled\":" +
	                                (restart_status.signaled ? "true" : "false") +
	                                ",\"output_dir\":\"" +
	                                json_escape(active_main_output_dir.string()) +
	                                "\",\"error\":\"" +
	                                json_escape(early_exit_reason) + "\"}");
	              } else {
	                append_line(events_path,
	                            std::string("{\"event\":\"main_afl_restarted_after_inline_micro\","
	                                        "\"pid\":") +
	                                std::to_string(main_process.pid) +
	                                ",\"output_dir\":\"" +
	                                json_escape(active_main_output_dir.string()) + "\"}");
	              }
	            } else {
	              std::cerr << "[DEBUG] Failed to restart main AFL: " << main_process.error << "\n";
	              early_exit_reason = "failed to restart AFL++ main campaign: " + main_process.error;
	              append_line(events_path,
	                          std::string("{\"event\":\"main_afl_restart_failed\",\"error\":\"") +
	                              json_escape(main_process.error) + "\"}");
            }
          } else {
            std::cerr << "[DEBUG] Micro campaigns prepared but not executed (dry_run or no specs)\n";
          }
        }
      }
    }


    ++inline_intervention_count;
    append_line(events_path,
                std::string("{\"event\":\"agent_inline_triggered\",\"plateau_id\":\"") +
                    json_escape(plateau.id) +
                    "\",\"decision_count\":" +
                    std::to_string(decisions.size()) +
                    ",\"total_inline\":" +
                    std::to_string(inline_intervention_count) + "}");
  };

  if (options.dry_run) {
    for (const auto& stats_path : options.main_stats_paths) {
      auto stats = read_stats_or_throw(stats_path);
      process_stats(stats);
    }
  } else {
    int elapsed_sec = 0;
    std::string last_telemetry_error;
    // Reserve a tail of the budget for post-loop cleanup. For short
    // 10-minute validation runs, keep substantive agent/micro work inline
    // and avoid stealing a full minute from the main fuzzing window.
    const int agent_reserve_sec = config.afl.main_budget_sec <= 900
        ? std::min(15, std::max(5, config.afl.main_budget_sec / 40))
        : std::min(1800, std::max(60, config.afl.main_budget_sec / 10));
    const int loop_budget_sec = std::max(60, config.afl.main_budget_sec - agent_reserve_sec);
    // Subscribe to the SIGINT/SIGTERM flag installed in main(). When
    // the signal arrives we break out of the main sampling loop,
    // tear down the AFL process group, and let the rest of run_mvp
    // run normal cleanup (DB finish_run, report writing).
    volatile sig_atomic_t* termination_flag = install_termination_signal_handler();
    while (elapsed_sec < loop_budget_sec) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      elapsed_sec += 1;

      if (termination_flag != nullptr && *termination_flag != 0) {
        append_line(events_path,
                    std::string("{\"event\":\"termination_signal\",\"signal\":") +
                        std::to_string(*termination_flag) + "}");
        // P0.4: hoisted flag so post-loop aggregation can classify exit.
        received_term_signal = true;
        break;
      }

      std::string error;
      const auto stats = collector.sample(&error);
      if (stats) {
        process_stats(*stats);
        // Heartbeat: synthesize a plateau if the natural detector has
        // not fired since the last agent trigger. Targets that keep
        // producing new edges (libxml2 in early hours) never plateau
        // and the inline-agent loop would otherwise degenerate to
        // baseline-AFL plus mutator overhead. Emit `plateau_detected`
        // (with reason="heartbeat") so the aggregator's plateau-count
        // acceptance gate counts heartbeats too; emit a sibling
        // `agent_heartbeat_triggered` so analyses can still tell the
        // two trigger paths apart.
        if (agent_inline_enabled && summary.plateau_id.empty() &&
            config.micro_campaign.agent_heartbeat_sec > 0 &&
            elapsed_sec - last_agent_trigger_at_sec >=
                config.micro_campaign.agent_heartbeat_sec) {
          PlateauEvent heartbeat;
          heartbeat.id = make_id("plateau_heartbeat");
          heartbeat.run_id = summary.run_id;
          heartbeat.campaign_id = summary.main_campaign_id;
          heartbeat.detected_ts = static_cast<uint64_t>(stats->sampled_at);
          heartbeat.window_sec = plateau_config.window_sec;
          heartbeat.reason = "heartbeat";
          summary.plateau_id = heartbeat.id;
          const auto blackboard = plateau_blackboard_json(heartbeat, *stats);
          db.insert_plateau(heartbeat, blackboard);
          append_line(events_path,
                      std::string("{\"event\":\"plateau_detected\",\"plateau\":") +
                          plateau_event_json(heartbeat) + "}");
          append_line(events_path,
                      std::string("{\"event\":\"agent_heartbeat_triggered\","
                                  "\"plateau_id\":\"") +
                          json_escape(heartbeat.id) +
                          "\",\"elapsed_sec\":" +
                          std::to_string(elapsed_sec) + "}");
        }
        if (agent_inline_enabled && !summary.plateau_id.empty()) {
          // Run the agent inline against this plateau, then reset the
          // detector so it can fire again later. AFL keeps running in the
          // background — this is the "closed-loop" path that lets a single
          // 24h run trigger many agent interventions instead of just one.
          PlateauEvent plateau_for_agent;
          plateau_for_agent.id = summary.plateau_id;
          plateau_for_agent.run_id = summary.run_id;
          plateau_for_agent.campaign_id = summary.main_campaign_id;
          plateau_for_agent.detected_ts =
              static_cast<uint64_t>(stats->sampled_at);
          plateau_for_agent.window_sec = plateau_config.window_sec;
          plateau_for_agent.reason = "inline_plateau";
          try {
            trigger_inline_agent(plateau_for_agent, *stats);
            last_agent_trigger_at_sec = elapsed_sec;
            // P0.2: Check if most recent decisions failed with auth_error.
            // Use 80% threshold (4 of 5) to catch both complete failures (bad
            // API key) and high partial failure rates (rate limits, quota issues).
            const auto recent = db.get_recent_decisions(summary.run_id, 5);
            if (!recent.empty()) {
              int auth_error_count = 0;
              for (const auto& decision_json : recent) {
                if (decision_json.find("\"error_kind\":\"auth_error\"") != std::string::npos) {
                  ++auth_error_count;
                }
              }
              const int threshold = std::max(1, static_cast<int>(recent.size() * 0.8));
              if (auth_error_count >= threshold) {
                std::ostringstream warning;
                warning << "WARNING: " << auth_error_count << " of " << recent.size()
                        << " recent LLM calls failed with authentication errors. "
                        << "Check API key and endpoint configuration.";
                std::cerr << warning.str() << std::endl;
                append_line(events_path,
                           std::string("{\"event\":\"llm_auth_failure_detected\",\"severity\":\"high\","
                                       "\"failed_count\":") + std::to_string(auth_error_count) +
                                       ",\"total_count\":" + std::to_string(recent.size()) +
                                       ",\"message\":\"" + json_escape(warning.str()) + "\"}");
              }
            }
          } catch (const std::exception& ex) {
            append_line(events_path,
                        std::string("{\"event\":\"agent_inline_failed\",\"error\":\"") +
                            json_escape(ex.what()) + "\"}");
          }
	          detector.reset();
	          summary.plateau_id.clear();
	          if (!early_exit_reason.empty()) {
	            break;
	          }
	        }
	      } else {
        last_telemetry_error = error;
        if (summary.main_pid > 0) {
          const auto status = wait_process(summary.main_pid, 0);
          if (status.exited || status.signaled) {
            std::ostringstream message;
            message << "AFL++ main campaign exited before telemetry was available";
            if (status.exited) {
              message << " with exit_code=" << status.exit_code;
            }
            if (status.signaled) {
              message << " from signal=" << status.term_signal;
            }
            if (!last_telemetry_error.empty()) {
              message << "; last telemetry error: " << last_telemetry_error;
            }
            append_line(events_path,
                        std::string("{\"event\":\"main_afl_exited_before_telemetry\","
                                    "\"exit_code\":") +
                            std::to_string(status.exit_code) + ",\"signaled\":" +
                            (status.signaled ? "true" : "false") + ",\"error\":\"" +
                            json_escape(last_telemetry_error) + "\"}");
            summary.main_pid = -1;
            // P0.3: do NOT throw — let the post-loop pipeline (forced
            // plateau, agent block, micro campaigns, report writing)
            // still run against whatever main_samples we accumulated.
            // The downstream "if (main_samples.empty()) throw" at the
            // top of the post-loop block still guards the no-data edge
            // case where AFL died before producing a single sample.
            early_exit_reason = message.str();
            append_line(events_path,
                        std::string("{\"event\":\"main_afl_early_exit\",\"reason\":\"") +
                            json_escape(early_exit_reason) + "\"}");
            break;
          }
        }
      }
    }
  }

  if (main_samples.empty()) {
    throw std::runtime_error(options.dry_run
                                 ? "dry-run requires at least one --stats sample"
                                 : "real run finished without any AFL++ telemetry samples");
  }

  // --- Stop AFL and capture the final telemetry sample ---
  if (!options.dry_run && summary.main_pid > 0) {
    const auto status = stop_afl_process(summary.main_pid, 5000);
    append_line(events_path,
                std::string("{\"event\":\"main_afl_stopped_for_analysis\",\"pid\":") +
                    std::to_string(summary.main_pid) + ",\"exited\":" +
                    (status.exited ? "true" : "false") + ",\"signaled\":" +
                    (status.signaled ? "true" : "false") + "}");
    summary.main_pid = -1;

    // Reset incremental state to bypass mtime cache and force parsing the newly written final stats file
    std::string final_error;
    collector.reset_incremental_state();
    const auto final_stats = collector.sample(&final_error);
    if (final_stats) {
      // Append the absolute final stats on exit to main_samples and events
      main_samples.push_back(*final_stats);
      db.insert_telemetry(summary.main_campaign_id, *final_stats);
      ++summary.telemetry_count;
      append_line(summary.coverage_csv_path, coverage_csv_row(*final_stats));
      append_line(events_path, telemetry_event_json(summary.run_id, summary.main_campaign_id, *final_stats));
    }
  }

  // P0.4: aggregate peaks across all collected samples. Done once here so
  // both the baseline early-return and the full-agent path see the same
  // honest peak numbers. paths_total / edges_found are monotone in AFL so
  // .back() would suffice in the happy path, but max() defends against
  // bogus rollbacks (e.g. AFL writes a half-flushed file before crash).
  {
    for (const auto& sample : main_samples) {
      if (sample.edges_found > summary.peak_bitmap_edges) {
        summary.peak_bitmap_edges = sample.edges_found;
      }
      if (sample.bitmap_cvg > summary.peak_coverage_pct) {
        summary.peak_coverage_pct = sample.bitmap_cvg;
      }
      if (sample.paths_total > summary.cumulative_corpus_items) {
        summary.cumulative_corpus_items = sample.paths_total;
      }
    }
    if (main_samples.size() >= 2) {
      const auto first_ts = static_cast<int64_t>(main_samples.front().sampled_at);
      const auto last_ts = static_cast<int64_t>(main_samples.back().sampled_at);
      summary.total_main_runtime_sec = last_ts - first_ts;
    }
    if (!early_exit_reason.empty()) {
      summary.main_afl_exit_reason = "early_exit";
    } else if (received_term_signal) {
      summary.main_afl_exit_reason = "signal_term";
    } else {
      summary.main_afl_exit_reason = "loop_budget";
    }
  }

  if (!config.micro_campaign.enabled) {
    std::vector<MicroResult> micro_results;
    std::vector<AgentDecision> decisions;
    write_report(summary, config, main_samples, micro_results, decisions);

    const auto done = static_cast<uint64_t>(std::time(nullptr));
    db.finish_campaign(summary.main_campaign_id, done, "completed", "completed");
    // Baseline runs never invoke the LLM and never reach winner selection,
    // so totals/status default to empty/zero. Recorded for completeness.
    RunLlmTotals totals;
    db.finish_run(summary.run_id, done, "completed",
                  to_string(summary.winner_status), totals);

    append_line(events_path, "{\"event\":\"m6_baseline_completed\",\"micro_campaigns_enabled\":false}");
    return summary;
  }
  if (summary.plateau_id.empty()) {
    PlateauEvent forced;
    forced.id = make_id("plateau");
    forced.run_id = summary.run_id;
    forced.campaign_id = summary.main_campaign_id;
    forced.detected_ts = static_cast<uint64_t>(main_samples.back().sampled_at);
    forced.window_sec = plateau_config.window_sec;
    forced.reason = "forced_mvp_plateau";
    summary.plateau_id = forced.id;

    const auto recent_decisions = db.get_recent_decisions(summary.run_id, 10);
    const auto agent_memory = db.get_agent_memory(summary.run_id);
    db.insert_plateau(forced, plateau_blackboard_json(forced, main_samples.back(), "{}", recent_decisions, agent_memory));
  }

  const auto snapshot_dir = summary.run_dir / "corpus_snapshot";
  const auto source_output = options.main_afl_output_dir.empty()
                                 ? (options.dry_run ? config.target.input_dir : active_main_output_dir)
                                 : options.main_afl_output_dir;
  const auto snapshot = snapshot_corpus(source_output, snapshot_dir);
  append_line(events_path, std::string("{\"event\":\"corpus_snapshot\",\"snapshot\":") +
                               corpus_snapshot_json(snapshot) + "}");

  // --- Static Analysis via configured reverse-engineering backend ---
  std::string static_context_json = "{}";
  std::filesystem::path static_dict_path;
  if (config.static_analysis.enabled) {
    if (!config.static_analysis.context_path.empty()) {
      static_context_json = base_intelligence_json;
      write_text_file(summary.run_dir / "static_context.json", static_context_json);
      append_line(events_path,
                  std::string("{\"event\":\"static_extractor_reused\","
                              "\"source\":\"precomputed_context\","
                              "\"context_size\":") +
                      std::to_string(static_context_json.size()) + "}");
    } else if (base_intelligence_json != "{}") {
      static_context_json = base_intelligence_json;
      write_text_file(summary.run_dir / "static_context.json", static_context_json);
      append_line(events_path,
                  std::string("{\"event\":\"static_extractor_reused\","
                              "\"source\":\"base_intelligence.json\","
                              "\"context_size\":") +
                      std::to_string(static_context_json.size()) + "}");
    } else {
      append_line(events_path,
                  std::string("{\"event\":\"static_extractor_started\",\"backend\":\"") +
                      json_escape(normalize_static_backend(config.static_analysis.backend)) + "\"}");
      static_context_json = run_static_extractor(
          config.static_analysis,
          std::filesystem::absolute(config.target.binary),
          summary.run_dir);
      append_line(events_path, std::string("{\"event\":\"static_extractor_done\",\"context_size\":") +
                                   std::to_string(static_context_json.size()) + "}");
    }

    // Generate AFL++ dictionary from static-analysis intelligence.
    static_dict_path = generate_dict_from_static_json(static_context_json, summary.run_dir);
    if (!static_dict_path.empty() && std::filesystem::exists(static_dict_path)) {
      append_line(events_path, std::string("{\"event\":\"static_dict_generated\",\"path\":\"") +
                                   static_dict_path.string() + "\"}");
    }
  }

  auto gateway = make_gateway(options, config);
  PlateauEvent blackboard_plateau;
  blackboard_plateau.id = summary.plateau_id;
  blackboard_plateau.run_id = summary.run_id;
  blackboard_plateau.campaign_id = summary.main_campaign_id;
  blackboard_plateau.detected_ts = static_cast<uint64_t>(main_samples.back().sampled_at);
  blackboard_plateau.window_sec = plateau_config.window_sec;
  blackboard_plateau.reason = "mvp_blackboard";

  const auto recent_decisions = db.get_recent_decisions(summary.run_id, 20);
  const auto agent_memory = db.get_agent_memory(summary.run_id);

  // Merge plateau-specific context with base intelligence.
  std::string combined_intel = options.suppress_semantic_context ? "{}" : base_intelligence_json;
  if (!options.suppress_semantic_context &&
      static_context_json != "{}" && static_context_json != base_intelligence_json) {
    combined_intel = static_context_json;
  }

  const auto blackboard = plateau_blackboard_json(blackboard_plateau, main_samples.back(), combined_intel, recent_decisions, agent_memory);
  auto tasks = make_default_agent_tasks(
      summary.plateau_id, blackboard,
      static_cast<uint32_t>(config.micro_campaign.budget_sec),
      format_few_shot_block());
  configure_agent_tasks(tasks, config, options);
  // Wall-clock cap: budget enough time for each sequential agent timeout
  // plus the provider stagger, capped at 30 min. A fixed 10%-of-run cap
  // skipped the latter half of the agent set on short GLM validation runs.
  const auto agent_deadline = agent_block_deadline_unix_sec(tasks);
  auto decisions = run_agent_tasks(*gateway, summary.run_id, summary.plateau_id, tasks, agent_deadline);
  for (const auto& decision : decisions) {
    persist_agent_decision(db, summary, config, decision, 0.0, events_path);
    reward_tracker.record_deploy(
        decision.id, decision.agent,
        short_proposal_summary(decision.proposal_json),
        static_cast<uint64_t>(decision.created_ts));
  }

  if (options.direct_promote_without_microcampaign) {
    const auto direct = select_direct_promotion_for_test(decisions);
    if (direct) {
      const auto direct_tokens =
          extract_dictionary_tokens_from_proposal(direct->proposal_json);
      summary.winner_status = WinnerStatus::kSelected;
      summary.winner_intervention_id = direct->id;
      summary.winner_campaign_id.clear();
      summary.promoted_recipe_index = write_promoted_recipe_index(
          summary.run_dir / "promoted_recipes", options, summary.run_id,
          direct->id, direct->agent, direct_tokens);
      append_line(events_path,
                  std::string("{\"event\":\"ai_direct_promoted\",\"ts\":") +
                      std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                      ",\"source\":\"post_loop_agent\","
                      "\"decision_id\":\"" +
                      json_escape(direct->id) +
                      "\",\"agent\":\"" +
                      json_escape(direct->agent) +
                      "\",\"token_count\":" +
                      std::to_string(direct_tokens.size()) +
                      ",\"recipe_index\":\"" +
                      json_escape(summary.promoted_recipe_index.string()) + "\"}");
    } else {
      summary.winner_status = WinnerStatus::kNoCandidates;
      append_line(events_path,
                  std::string("{\"event\":\"ai_direct_no_candidate\",\"ts\":") +
                      std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                      ",\"source\":\"post_loop_agent\"}");
    }

    std::vector<MicroResult> micro_results = layered_micro_results;
    write_report(summary, config, main_samples, micro_results, decisions);
    const auto done = static_cast<uint64_t>(std::time(nullptr));
    db.finish_campaign(summary.main_campaign_id, done, "completed", "completed");
    RunLlmTotals totals;
    totals.calls = summary.llm_calls;
    totals.failed_calls = summary.llm_failed_calls;
    totals.input_tokens = summary.llm_input_tokens;
    totals.output_tokens = summary.llm_output_tokens;
    totals.total_latency_ms = summary.llm_total_latency_ms;
    db.finish_run(summary.run_id, done, "completed",
                  to_string(summary.winner_status), totals);
    return summary;
  }

  const auto specs = plan_micro_campaigns(
      config, summary.plateau_id, snapshot_dir, summary.run_dir / "micro", options.dry_run, &db, summary.run_id);
  std::vector<std::string> micro_tokens;
  for (const auto& d : decisions) {
    micro_tokens.insert(micro_tokens.end(), d.extracted_tokens.begin(), d.extracted_tokens.end());
  }
  prepare_micro_campaigns(specs, micro_tokens);
  summary.micro_campaign_count = specs.size();

  if (!options.dry_run && summary.main_pid > 0) {
    const auto status = stop_afl_process(summary.main_pid, 5000);
    append_line(events_path, std::string("{\"event\":\"main_afl_stopped_for_micro\",\"pid\":") +
                                 std::to_string(summary.main_pid) +
                                 ",\"exited\":" + (status.exited ? "true" : "false") +
                                 ",\"signaled\":" + (status.signaled ? "true" : "false") + "}");
    summary.main_pid = -1;
  }

  std::vector<MicroResult> micro_results = layered_micro_results;
  // Track failed micro campaigns for exclusion from winner selection. This
  // set is populated in the single-threaded sequential loop below (line 1219)
  // and read during winner selection (line 1313), so no synchronization needed.
  std::set<std::string> failed_micro_campaigns;
  // Micro campaigns start fresh from a corpus snapshot with their own AFL++
  // instance. The main `parent_stats` (from the full run) has accumulated
  // thousands of edges, making all micro deltas saturate to zero (new_edges = 0).
  // Instead, use a zero-baseline synthetic parent so we measure absolute
  // edges/paths each micro campaign found — enabling meaningful relative
  // comparison between competing intervention strategies.
  AflStats micro_parent_baseline;  // Zero-initialized: all counters = 0
  const auto parent_stats = micro_parent_baseline;
  // Resolve reward mode for this run (CLI / ablation override).
  const RewardMode reward_mode = reward_mode_from_string(options.reward_mode);
  struct MicroProcessInfo {
    std::size_t index;
    uint64_t campaign_start_ts;
    int pid = -1;
    std::string spawn_error;
    bool is_dry_run = false;
    AflStats dry_run_stats;
  };

  std::vector<MicroProcessInfo> launched_micros;
  launched_micros.reserve(specs.size());

  // Loop 1: Concurrently spawn all micro campaigns
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const auto& spec = specs[i];
    const auto campaign_start_ts = static_cast<uint64_t>(std::time(nullptr));
    db.insert_campaign(spec.id, summary.run_id, "micro", summary.main_campaign_id,
                       spec.intervention_id, spec.output_dir, campaign_start_ts, spec.budget_sec,
                       options.dry_run ? "dry_run" : "running");

    MicroProcessInfo info;
    info.index = i;
    info.campaign_start_ts = campaign_start_ts;
    info.is_dry_run = options.dry_run;

    if (options.dry_run) {
      if (options.micro_stats_paths.empty() && options.main_stats_paths.empty()) {
        throw std::runtime_error(
            "dry_run requires --stats (main) or --micro-stats; both are empty");
      }
      const auto stats_path = options.micro_stats_paths.empty()
                                  ? options.main_stats_paths.back()
                                  : options.micro_stats_paths[std::min(i, options.micro_stats_paths.size() - 1)];
      info.dry_run_stats = read_stats_or_throw(stats_path);
    } else {
      const auto micro_launch = build_micro_afl_spec(config, spec, static_dict_path);
      write_text_file(spec.output_dir / "launch.sh", "#!/usr/bin/env sh\n" + shell_preview(micro_launch) + "\n");
      const auto process = spawn_process(micro_launch.afl_fuzz.string(), micro_launch.argv, micro_launch.env);
      if (process.pid > 0) {
        info.pid = process.pid;
        append_line(events_path, std::string("{\"event\":\"micro_afl_launched\",\"pid\":") +
                                     std::to_string(process.pid) + ",\"campaign_id\":\"" + spec.id + "\"}");
      } else {
        info.pid = -1;
        info.spawn_error = process.error;
      }
    }
    launched_micros.push_back(info);
  }

  // Loop 2: Sequentially wait, parse stats, and evaluate results
  for (const auto& info : launched_micros) {
    const auto& spec = specs[info.index];
    AflStats micro_stats;
    std::string termination_reason = "completed";
    bool have_micro_stats = false;

    if (info.is_dry_run) {
      micro_stats = info.dry_run_stats;
      termination_reason = "dry_run";
      have_micro_stats = true;
    } else {
      if (info.pid > 0) {
        // Dynamically calculate remaining budget timeout from spawning time
        const uint64_t now_ts = static_cast<uint64_t>(std::time(nullptr));
        const int elapsed_sec = static_cast<int>(now_ts - info.campaign_start_ts);
        const int max_allowed_sec = spec.budget_sec + 5;
        const int remaining_ms = std::max(0, (max_allowed_sec - elapsed_sec) * 1000);

        const auto status = wait_process(info.pid, remaining_ms);
        if (!status.exited && !status.signaled) {
          stop_afl_process(info.pid, 3000);
          termination_reason = "timeout_killed";
        } else if (status.signaled) {
          termination_reason = "signaled";
        }

        std::string error;
        auto live_stats = parse_fuzzer_stats(spec.output_dir / "default" / "fuzzer_stats", &error);
        if (live_stats) {
          micro_stats = *live_stats;
          have_micro_stats = true;
        } else {
          const auto fail_ts = static_cast<uint64_t>(std::time(nullptr));
          db.finish_campaign(spec.id, fail_ts, "failed");
          failed_micro_campaigns.insert(spec.id);
          append_line(events_path,
                      std::string("{\"event\":\"micro_afl_failed\",\"campaign_id\":\"") +
                          json_escape(spec.id) + "\",\"intervention_id\":\"" +
                          json_escape(spec.intervention_id) + "\",\"start_ts\":" +
                          std::to_string(info.campaign_start_ts) + ",\"end_ts\":" +
                          std::to_string(fail_ts) + ",\"reason\":\"stats_unreadable\",\"error\":\"" +
                          json_escape(error) + "\"}");
          termination_reason = "stats_unreadable";
        }
      } else {
        const auto fail_ts = static_cast<uint64_t>(std::time(nullptr));
        db.finish_campaign(spec.id, fail_ts, "failed");
        failed_micro_campaigns.insert(spec.id);
        append_line(events_path,
                    std::string("{\"event\":\"micro_afl_spawn_failed\",\"campaign_id\":\"") +
                        json_escape(spec.id) + "\",\"intervention_id\":\"" +
                        json_escape(spec.intervention_id) + "\",\"start_ts\":" +
                        std::to_string(info.campaign_start_ts) + ",\"end_ts\":" +
                        std::to_string(fail_ts) + ",\"reason\":\"spawn_failed\",\"error\":\"" +
                        json_escape(info.spawn_error) + "\"}");
        termination_reason = "spawn_failed";
      }
    }

    const auto campaign_end_ts = static_cast<uint64_t>(std::time(nullptr));
    append_line(events_path, std::string("{\"event\":\"micro_campaign_completed\",\"campaign_id\":\"") +
                                 json_escape(spec.id) + "\",\"start_ts\":" +
                                 std::to_string(info.campaign_start_ts) + ",\"end_ts\":" +
                                 std::to_string(campaign_end_ts) + ",\"duration_sec\":" +
                                 std::to_string(campaign_end_ts - info.campaign_start_ts) +
                                 ",\"termination_reason\":\"" + termination_reason + "\"}");
    if (!have_micro_stats) {
      continue;
    }
    auto result = evaluate_micro_result(spec.intervention_id, spec.id, parent_stats,
                                        micro_stats, reward_mode);
    if (!should_persist_micro_result(result, failed_micro_campaigns)) {
      continue;
    }
    micro_results.push_back(result);
  }

  // Winner selection with explicit status. Stage-0 default_control is
  // the control arm: an agent campaign only wins if it beats that
  // baseline by a meaningful margin. This prevents "promoting" the
  // control campaign, which is indistinguishable from no useful agent.
  const auto winner_selection = select_micro_winner_against_control(
      micro_results, specs, failed_micro_campaigns);
  summary.micro_campaigns_failed = failed_micro_campaigns.size();
  if (micro_results.empty()) {
    summary.winner_status = WinnerStatus::kNoCandidates;
  } else if (!winner_selection.has_valid_results) {
    summary.winner_status = WinnerStatus::kAllFailed;
  } else if (winner_selection.selected &&
             winner_selection.result_index < micro_results.size()) {
    auto& winner = micro_results[winner_selection.result_index];
    winner.promoted = true;
    summary.winner_intervention_id = winner.intervention_id;
    summary.winner_campaign_id = winner.campaign_id;
    summary.winner_reward = winner_selection.improvement_over_control;
    summary.winner_status = WinnerStatus::kSelected;
  } else {
    summary.winner_status = WinnerStatus::kNoSignificance;
  }
  append_line(events_path, std::string("{\"event\":\"winner_decided\",\"ts\":") +
                               std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                               ",\"status\":\"" +
                               to_string(summary.winner_status) +
                               "\",\"valid_results\":" +
                               std::to_string(winner_selection.valid_results) +
                               ",\"failed_results\":" +
                               std::to_string(summary.micro_campaigns_failed) +
                               ",\"control_reward\":" +
                               std::to_string(winner_selection.control_reward) +
                               ",\"improvement_over_control\":" +
                               std::to_string(winner_selection.improvement_over_control) +
                               ",\"winner_reward\":" +
                               std::to_string(summary.winner_reward) + "}");

  for (const auto& result : micro_results) {
    db.insert_micro_result(result);
    if (failed_micro_campaigns.find(result.campaign_id) == failed_micro_campaigns.end()) {
      db.finish_campaign(result.campaign_id, static_cast<uint64_t>(std::time(nullptr)), "completed");
    }
    append_line(events_path, std::string("{\"event\":\"micro_result\",\"result\":") +
                                 micro_result_json(result) + "}");
  }

  AgentTask result_task;
  result_task.task_id = make_id("agent_task");
  result_task.agent_name = "ResultAnalysisAgent";
  result_task.objective =
      "Summarize validated micro-campaign rewards and produce agent memory patches";
  result_task.blackboard_slice_json =
      std::string("{\"original_blackboard\":") + blackboard +
      ",\"micro_campaign_results\":{\"winner_intervention_id\":\"" +
      json_escape(summary.winner_intervention_id) + "\",\"winner_reward\":" +
      std::to_string(summary.winner_reward) + ",\"all_results\":" +
      micro_results_json(micro_results) + "}}";
  result_task.action_schema_json =
      "{\"allowed_actions\":[\"memory_patch\",\"priority_update\",\"keep_winner\"]}";
  result_task.output_schema_json =
      "{\"required\":[\"agent\",\"memory_patch\",\"critique\"]}";
  result_task.budget_sec = static_cast<uint32_t>(config.micro_campaign.budget_sec);
  result_task.timeout_ms = static_cast<uint32_t>(std::max(1000, config.agent_runtime.per_agent_timeout_ms));
  result_task.max_output_tokens = static_cast<uint32_t>(std::max(256, config.model_api.max_output_tokens));
  const auto result_decisions = run_agent_tasks(
      *gateway, summary.run_id, summary.plateau_id, {result_task});
  for (const auto& decision : result_decisions) {
    persist_agent_decision(db, summary, config, decision, summary.winner_reward, events_path);
    reward_tracker.record_deploy(
        decision.id, decision.agent,
        short_proposal_summary(decision.proposal_json),
        static_cast<uint64_t>(decision.created_ts));
    decisions.push_back(decision);
  }

  if (!summary.winner_intervention_id.empty()) {
    // Build the recipe to promote. For the `random-recipe` ablation we
    // bypass the agent-driven dictionary strategy entirely and emit a
    // recipe whose operator weights are sampled at random — this is
    // what makes the ablation actually compare "agent recipes vs
    // random recipes" rather than silently fall through to the agent
    // strategy (which was the bug noted in the post-fix review).
    auto all_proposals = db.get_recent_decisions(summary.run_id, 1000);
    std::vector<std::string> all_tokens;
    std::set<std::string> unique_tokens;
    std::string source_agent = "AgentCouncil";
    for (const auto& prop : all_proposals) {
      if (prop.find(summary.winner_intervention_id) != std::string::npos) {
        source_agent = "ValidatedAgentProposal";
      }
      auto tokens = extract_dictionary_tokens_from_proposal(prop);
      for (const auto& t : tokens) {
        if (unique_tokens.find(t) == unique_tokens.end()) {
          unique_tokens.insert(t);
          all_tokens.push_back(t);
        }
      }
    }
    summary.promoted_recipe_index = write_promoted_recipe_index(
        summary.run_dir / "promoted_recipes", options, summary.run_id,
        summary.winner_intervention_id, source_agent, all_tokens);
    append_line(events_path, std::string("{\"event\":\"promotion\",\"ts\":") +
                                 std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                                 ",\"winner_intervention_id\":\"" +
                                 json_escape(summary.winner_intervention_id) +
                                 "\",\"recipe_source\":\"" +
                                 json_escape(options.recipe_source) +
                                 "\",\"recipe_index\":\"" +
                                 json_escape(summary.promoted_recipe_index.string()) + "\"}");
  } else {
    append_line(events_path, std::string("{\"event\":\"promotion_skipped\",\"ts\":") +
                                 std::to_string(static_cast<uint64_t>(std::time(nullptr))) +
                                 ",\"reason\":\"no_successful_micro_campaign\"}");
  }

  write_report(summary, config, main_samples, micro_results, decisions);

  const auto done = static_cast<uint64_t>(std::time(nullptr));
  db.finish_campaign(summary.main_campaign_id, done, "completed", "completed");
  // Persist aggregate LLM accounting collected during the run.
  RunLlmTotals totals;
  totals.calls = summary.llm_calls;
  totals.failed_calls = summary.llm_failed_calls;
  totals.input_tokens = summary.llm_input_tokens;
  totals.output_tokens = summary.llm_output_tokens;
  totals.total_latency_ms = summary.llm_total_latency_ms;
  db.finish_run(summary.run_id, done, "completed",
                to_string(summary.winner_status), totals);

  if (!options.dry_run && summary.main_pid > 0) {
    stop_afl_process(summary.main_pid, 5000);
  }

  return summary;
}

std::string run_summary_json(const RunSummary& summary) {
  std::ostringstream out;
  out << "{";
  out << "\"run_id\":\"" << json_escape(summary.run_id) << "\",";
  out << "\"main_campaign_id\":\"" << json_escape(summary.main_campaign_id) << "\",";
  out << "\"plateau_id\":\"" << json_escape(summary.plateau_id) << "\",";
  out << "\"winner_intervention_id\":\"" << json_escape(summary.winner_intervention_id) << "\",";
  out << "\"winner_campaign_id\":\"" << json_escape(summary.winner_campaign_id) << "\",";
  out << "\"winner_status\":\"" << to_string(summary.winner_status) << "\",";
  out << "\"ablation_mode\":\"" << json_escape(summary.ablation_mode) << "\",";
  out << "\"winner_reward\":" << summary.winner_reward << ",";
  out << "\"run_dir\":\"" << json_escape(summary.run_dir.string()) << "\",";
  out << "\"db_path\":\"" << json_escape(summary.db_path.string()) << "\",";
  out << "\"report_path\":\"" << json_escape(summary.report_path.string()) << "\",";
  out << "\"coverage_csv_path\":\"" << json_escape(summary.coverage_csv_path.string()) << "\",";
  out << "\"agent_replay_log_path\":\"" << json_escape(summary.agent_replay_log_path.string()) << "\",";
  out << "\"agent_memory_path\":\"" << json_escape(summary.agent_memory_path.string()) << "\",";
  out << "\"main_launch_path\":\"" << json_escape(summary.main_launch_path.string()) << "\",";
  out << "\"promoted_recipe_index\":\"" << json_escape(summary.promoted_recipe_index.string()) << "\",";
  out << "\"telemetry_count\":" << summary.telemetry_count << ",";
  out << "\"agent_decision_count\":" << summary.agent_decision_count << ",";
  out << "\"micro_campaign_count\":" << summary.micro_campaign_count << ",";
  out << "\"micro_campaigns_failed\":" << summary.micro_campaigns_failed << ",";
  out << "\"llm_calls\":" << summary.llm_calls << ",";
  out << "\"llm_input_tokens\":" << summary.llm_input_tokens << ",";
  out << "\"llm_output_tokens\":" << summary.llm_output_tokens << ",";
  out << "\"llm_failed_calls\":" << summary.llm_failed_calls << ",";
  out << "\"llm_total_latency_ms\":" << summary.llm_total_latency_ms << ",";
  out << "\"main_pid\":" << summary.main_pid << ",";
  // P0.4: honest aggregates across all collected samples. Old field
  // telemetry_count above is sample-count of successful stats reads;
  // these new fields are the actual coverage / corpus peaks reached
  // even if AFL later crashed and reset its visible stats.
  out << "\"peak_bitmap_edges\":" << summary.peak_bitmap_edges << ",";
  out << "\"peak_coverage_pct\":" << summary.peak_coverage_pct << ",";
  out << "\"cumulative_corpus_items\":" << summary.cumulative_corpus_items << ",";
  out << "\"total_main_runtime_sec\":" << summary.total_main_runtime_sec << ",";
  out << "\"main_afl_exit_reason\":\"" << json_escape(summary.main_afl_exit_reason) << "\"";
  out << "}";
  return out.str();
}

const char* to_string(WinnerStatus status) {
  switch (status) {
    case WinnerStatus::kNoCandidates: return "no_candidates";
    case WinnerStatus::kAllFailed: return "all_failed";
    case WinnerStatus::kSelected: return "selected";
    case WinnerStatus::kNoSignificance: return "no_significance";
  }
  return "unknown";
}

}  // namespace fuzzpilot
