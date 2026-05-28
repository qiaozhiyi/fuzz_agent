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
#include <memory>
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
  } else if (options.ablation_mode != "full-agent") {
    throw std::runtime_error("unsupported ablation mode: " + options.ablation_mode);
  }
}

void configure_agent_tasks(std::vector<AgentTask>& tasks, const AppConfig& config) {
  for (auto& task : tasks) {
    task.timeout_ms = static_cast<uint32_t>(std::max(1000, config.agent_runtime.per_agent_timeout_ms));
    task.max_output_tokens = static_cast<uint32_t>(std::max(256, config.model_api.max_output_tokens));
  }
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
    main_store.write_compact_recipes({make_default_dictionary_strategy({"FUZZ", "MAGIC", "TOKEN"})});
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
    if (!std::filesystem::exists(intel_path)) {
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

  auto trigger_inline_agent = [&](const PlateauEvent& plateau,
                                   const AflStats& stats) {
    const auto recent = db.get_recent_decisions(summary.run_id, 10);
    const auto mem = db.get_agent_memory(summary.run_id);
    const auto bb = plateau_blackboard_json(plateau, stats,
                                            base_intelligence_json,
                                            recent, mem);
    auto tasks = make_default_agent_tasks(
        plateau.id, bb,
        static_cast<uint32_t>(config.micro_campaign.budget_sec),
        format_few_shot_block());
    configure_agent_tasks(tasks, config);
    const auto deadline =
        static_cast<uint64_t>(std::time(nullptr)) + 300;
    auto decisions = run_agent_tasks(*inline_gateway, summary.run_id,
                                     plateau.id, tasks, deadline);
    for (const auto& d : decisions) {
      persist_agent_decision(db, summary, config, d, 0.0, events_path);
      reward_tracker.record_deploy(
          d.id, d.agent, short_proposal_summary(d.proposal_json),
          static_cast<uint64_t>(d.created_ts));
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
    TelemetryCollector collector(main_output_dir, "");
    int elapsed_sec = 0;
    std::string last_telemetry_error;
    // Reserve a tail of the budget for the post-loop agent pipeline
    // (forced plateau + run_agent_tasks + micro campaigns + DB cleanup).
    // Without this, a long fuzz with no plateau detection eats the
    // entire budget and the agent block races SIGTERM. 30 min for
    // long runs, 10% for short ones — whichever is smaller.
    const int agent_reserve_sec = std::min(
        1800, std::max(60, config.afl.main_budget_sec / 10));
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
          } catch (const std::exception& ex) {
            append_line(events_path,
                        std::string("{\"event\":\"agent_inline_failed\",\"error\":\"") +
                            json_escape(ex.what()) + "\"}");
          }
          detector.reset();
          summary.plateau_id.clear();
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
            throw std::runtime_error(message.str());
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
  if (!options.dry_run && config.micro_campaign.enabled && summary.main_pid > 0) {
    const auto status = stop_afl_process(summary.main_pid, 5000);
    append_line(events_path,
                std::string("{\"event\":\"main_afl_stopped_for_analysis\",\"pid\":") +
                    std::to_string(summary.main_pid) + ",\"exited\":" +
                    (status.exited ? "true" : "false") + ",\"signaled\":" +
                    (status.signaled ? "true" : "false") + "}");
    summary.main_pid = -1;
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

    if (!options.dry_run && summary.main_pid > 0) {
      stop_afl_process(summary.main_pid, 5000);
      summary.main_pid = -1;
    }
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
                                 ? (options.dry_run ? config.target.input_dir : main_output_dir)
                                 : options.main_afl_output_dir;
  const auto snapshot = snapshot_corpus(source_output, snapshot_dir);
  append_line(events_path, std::string("{\"event\":\"corpus_snapshot\",\"snapshot\":") +
                               corpus_snapshot_json(snapshot) + "}");

  // --- Static Analysis via configured reverse-engineering backend ---
  std::string static_context_json = "{}";
  std::filesystem::path static_dict_path;
  if (config.static_analysis.enabled) {
    if (base_intelligence_json != "{}") {
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
  std::string combined_intel = base_intelligence_json;
  if (static_context_json != "{}" && static_context_json != base_intelligence_json) {
    combined_intel = static_context_json;
  }

  const auto blackboard = plateau_blackboard_json(blackboard_plateau, main_samples.back(), combined_intel, recent_decisions, agent_memory);
  auto tasks = make_default_agent_tasks(
      summary.plateau_id, blackboard,
      static_cast<uint32_t>(config.micro_campaign.budget_sec),
      format_few_shot_block());
  configure_agent_tasks(tasks, config);
  // Wall-clock cap: budget the agent block to roughly the reserve window
  // we held back in the main loop, capped at 30 min. Prevents 8 sequential
  // LLM calls from blowing past the SIGTERM cut-off.
  const auto agent_deadline =
      static_cast<uint64_t>(std::time(nullptr)) +
      static_cast<uint64_t>(std::min(1800, std::max(60, config.afl.main_budget_sec / 10)));
  auto decisions = run_agent_tasks(*gateway, summary.run_id, summary.plateau_id, tasks, agent_deadline);
  for (const auto& decision : decisions) {
    persist_agent_decision(db, summary, config, decision, 0.0, events_path);
    reward_tracker.record_deploy(
        decision.id, decision.agent,
        short_proposal_summary(decision.proposal_json),
        static_cast<uint64_t>(decision.created_ts));
  }

  const auto specs = plan_micro_campaigns(
      config, summary.plateau_id, snapshot_dir, summary.run_dir / "micro", options.dry_run);
  prepare_micro_campaigns(specs);
  summary.micro_campaign_count = specs.size();

  if (!options.dry_run && summary.main_pid > 0) {
    const auto status = stop_afl_process(summary.main_pid, 5000);
    append_line(events_path, std::string("{\"event\":\"main_afl_stopped_for_micro\",\"pid\":") +
                                 std::to_string(summary.main_pid) +
                                 ",\"exited\":" + (status.exited ? "true" : "false") +
                                 ",\"signaled\":" + (status.signaled ? "true" : "false") + "}");
    summary.main_pid = -1;
  }

  std::vector<MicroResult> micro_results;
  std::set<std::string> failed_micro_campaigns;
  const auto parent_stats = main_samples.back();
  // Resolve reward mode for this run (CLI / ablation override).
  RewardMode reward_mode = RewardMode::kEdgeWeighted;
  if (options.reward_mode == "edges_only") {
    reward_mode = RewardMode::kEdgesOnly;
  } else if (options.reward_mode == "paths_only") {
    reward_mode = RewardMode::kPathsOnly;
  } else if (options.reward_mode == "random") {
    reward_mode = RewardMode::kRandom;
  }
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const auto& spec = specs[i];
    const auto campaign_start_ts = static_cast<uint64_t>(std::time(nullptr));
    db.insert_campaign(spec.id, summary.run_id, "micro", summary.main_campaign_id,
                       spec.intervention_id, spec.output_dir, campaign_start_ts, spec.budget_sec,
                       options.dry_run ? "dry_run" : "running");

    AflStats micro_stats;
    std::string termination_reason = "completed";
    if (options.dry_run) {
      // In dry-run mode the controller fakes AFL execution by reading
      // pre-recorded stats files. If neither micro nor main stats paths
      // were provided we have nothing to read — fail loudly rather
      // than calling .back() on an empty vector (which is UB).
      if (options.micro_stats_paths.empty() && options.main_stats_paths.empty()) {
        throw std::runtime_error(
            "dry_run requires --stats (main) or --micro-stats; both are empty");
      }
      const auto stats_path = options.micro_stats_paths.empty()
                                  ? options.main_stats_paths.back()
                                  : options.micro_stats_paths[std::min(i, options.micro_stats_paths.size() - 1)];
      micro_stats = read_stats_or_throw(stats_path);
      termination_reason = "dry_run";
    } else {
      const auto micro_launch = build_micro_afl_spec(config, spec, static_dict_path);
      write_text_file(spec.output_dir / "launch.sh", "#!/usr/bin/env sh\n" + shell_preview(micro_launch) + "\n");
      const auto process = spawn_process(micro_launch.afl_fuzz.string(), micro_launch.argv, micro_launch.env);
      if (process.pid > 0) {
        append_line(events_path, std::string("{\"event\":\"micro_afl_launched\",\"pid\":") +
                                     std::to_string(process.pid) + ",\"campaign_id\":\"" + spec.id + "\"}");
        const auto status = wait_process(process.pid, spec.budget_sec * 1000 + 5000);
        if (!status.exited && !status.signaled) {
          stop_afl_process(process.pid, 3000);
          termination_reason = "timeout_killed";
        } else if (status.signaled) {
          termination_reason = "signaled";
        }
        std::string error;
        auto live_stats = parse_fuzzer_stats(spec.output_dir / "default" / "fuzzer_stats", &error);
        if (live_stats) {
          micro_stats = *live_stats;
        } else {
          const auto fail_ts = static_cast<uint64_t>(std::time(nullptr));
          db.finish_campaign(spec.id, fail_ts, "failed");
          failed_micro_campaigns.insert(spec.id);
          append_line(events_path,
                      std::string("{\"event\":\"micro_afl_failed\",\"campaign_id\":\"") +
                          json_escape(spec.id) + "\",\"intervention_id\":\"" +
                          json_escape(spec.intervention_id) + "\",\"start_ts\":" +
                          std::to_string(campaign_start_ts) + ",\"end_ts\":" +
                          std::to_string(fail_ts) + ",\"reason\":\"stats_unreadable\",\"error\":\"" +
                          json_escape(error) + "\"}");
          micro_stats = parent_stats; // fallback if failed to start/write
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
                        std::to_string(campaign_start_ts) + ",\"end_ts\":" +
                        std::to_string(fail_ts) + ",\"reason\":\"spawn_failed\",\"error\":\"" +
                        json_escape(process.error) + "\"}");
        micro_stats = parent_stats;
        termination_reason = "spawn_failed";
      }
    }
    auto result = evaluate_micro_result(spec.intervention_id, spec.id, parent_stats,
                                        micro_stats, reward_mode);
    const auto campaign_end_ts = static_cast<uint64_t>(std::time(nullptr));
    append_line(events_path, std::string("{\"event\":\"micro_campaign_completed\",\"campaign_id\":\"") +
                                 json_escape(spec.id) + "\",\"start_ts\":" +
                                 std::to_string(campaign_start_ts) + ",\"end_ts\":" +
                                 std::to_string(campaign_end_ts) + ",\"duration_sec\":" +
                                 std::to_string(campaign_end_ts - campaign_start_ts) +
                                 ",\"termination_reason\":\"" + termination_reason + "\"}");
    micro_results.push_back(result);
  }

  // Winner selection with explicit status — distinguishes "no candidates"
  // from "all failed" from "selected" so downstream analysis doesn't
  // conflate them.
  auto winner_it = micro_results.end();
  uint64_t valid_results = 0;
  for (auto it = micro_results.begin(); it != micro_results.end(); ++it) {
    if (failed_micro_campaigns.find(it->campaign_id) != failed_micro_campaigns.end()) {
      continue;
    }
    ++valid_results;
    if (winner_it == micro_results.end() || it->reward > winner_it->reward) {
      winner_it = it;
    }
  }
  summary.micro_campaigns_failed = failed_micro_campaigns.size();
  if (micro_results.empty()) {
    summary.winner_status = WinnerStatus::kNoCandidates;
  } else if (winner_it == micro_results.end()) {
    summary.winner_status = WinnerStatus::kAllFailed;
  } else {
    // Naive significance check: require winner to beat the second-best by
    // at least 5% of its own reward, or by an absolute small margin when
    // reward magnitudes are tiny. Replaces the previous raw-comparison
    // behaviour that promoted on any positive delta (which is statistical
    // noise). For full significance use --micro-campaign-repeats >= 3 and
    // run the offline Mann-Whitney pass.
    double second_best = 0.0;
    bool have_second = false;
    for (const auto& r : micro_results) {
      if (failed_micro_campaigns.count(r.campaign_id)) continue;
      if (&r == &(*winner_it)) continue;
      if (!have_second || r.reward > second_best) {
        second_best = r.reward;
        have_second = true;
      }
    }
    const double abs_margin = 0.5;       // arbitrary small-reward floor
    const double rel_margin = 0.05;      // 5% relative margin
    const bool significant = !have_second ||
        (winner_it->reward - second_best) >
            std::max(abs_margin, std::abs(winner_it->reward) * rel_margin);
    if (significant && winner_it->reward > 0.0) {
      winner_it->promoted = true;
      summary.winner_intervention_id = winner_it->intervention_id;
      summary.winner_campaign_id = winner_it->campaign_id;
      summary.winner_reward = winner_it->reward;
      summary.winner_status = WinnerStatus::kSelected;
    } else {
      summary.winner_status = WinnerStatus::kNoSignificance;
    }
  }
  append_line(events_path, std::string("{\"event\":\"winner_decided\",\"status\":\"") +
                               to_string(summary.winner_status) +
                               "\",\"valid_results\":" + std::to_string(valid_results) +
                               ",\"failed_results\":" +
                               std::to_string(summary.micro_campaigns_failed) +
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
    SeedMutationStrategy promoted;
    if (options.recipe_source == "random") {
      // Seed the RNG from the run_id hash so the same run reproduces
      // the same recipe; different runs differ.
      uint64_t seed_value = std::hash<std::string>{}(summary.run_id);
      promoted = make_random_recipe_strategy(
          seed_value, {"PROMOTED", summary.winner_intervention_id, "FUZZ", "TOKEN"});
    } else {
      promoted = make_default_dictionary_strategy(
          {"PROMOTED", summary.winner_intervention_id, "FUZZ", "TOKEN"});
    }
    promoted.id = make_id("strategy_promoted");
    RecipeStore promoted_store(summary.run_dir / "promoted_recipes");
    summary.promoted_recipe_index = promoted_store.write_compact_recipes({promoted});
    append_line(events_path, std::string("{\"event\":\"promotion\",\"winner_intervention_id\":\"") +
                                 json_escape(summary.winner_intervention_id) +
                                 "\",\"recipe_source\":\"" +
                                 json_escape(options.recipe_source) +
                                 "\",\"recipe_index\":\"" +
                                 json_escape(summary.promoted_recipe_index.string()) + "\"}");
  } else {
    append_line(events_path, "{\"event\":\"promotion_skipped\",\"reason\":\"no_successful_micro_campaign\"}");
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
  out << "\"main_pid\":" << summary.main_pid;
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
