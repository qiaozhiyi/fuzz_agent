#include "fuzzpilot/controller/run.hpp"

#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/config.hpp"
#include "fuzzpilot/env.hpp"
#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/micro/manager.hpp"
#include "fuzzpilot/model/gateway.hpp"
#include "fuzzpilot/mutation/recipe_store.hpp"
#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/runner/afl_runner.hpp"
#include "fuzzpilot/runner/process.hpp"
#include "fuzzpilot/storage/db.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {
namespace {

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      default: out << c; break;
    }
  }
  return out.str();
}

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

std::string plateau_blackboard_json(const PlateauEvent& plateau, const AflStats& stats) {
  return std::string("{\"plateau\":") + plateau_event_json(plateau) +
         ",\"main_metrics\":" + afl_stats_json(stats) +
         ",\"available_actions\":[\"default_control\",\"dictionary_probe\","
         "\"seed_focus_probe\",\"per_seed_recipe_probe\"]}";
}

std::string coverage_csv_row(const AflStats& stats) {
  return std::to_string(stats.sampled_at) + "," +
         std::to_string(stats.execs_done) + "," +
         std::to_string(stats.execs_per_sec) + "," +
         std::to_string(stats.paths_total) + "," +
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

std::unique_ptr<IModelGateway> make_gateway(const RunOptions& options) {
  if (options.model_provider == "fake") {
    return std::make_unique<FakeModelGateway>();
  }
  if (options.model_provider == "openai-compatible") {
    return std::make_unique<OpenAICompatibleGateway>(
        options.model_endpoint, options.model_name, options.api_key_env, true);
  }
  throw std::runtime_error("unsupported model provider: " + options.model_provider);
}

void persist_agent_memory(Database& db,
                          const RunSummary& summary,
                          const AppConfig& config,
                          const AgentDecision& decision,
                          double reward_hint) {
  const auto now = static_cast<uint64_t>(std::time(nullptr));
  const auto key = summary.plateau_id + ":" + decision.agent + ":" +
                   decision.model_response.response_hash;
  const auto evidence = std::string("{\"decision_id\":\"") + json_escape(decision.id) +
                        "\",\"context_hash\":\"" +
                        json_escape(decision.model_response.context_hash) +
                        "\",\"schema_valid\":" +
                        (decision.model_response.schema_valid ? "true" : "false") + "}";
  db.insert_agent_memory(make_id("memory"), summary.run_id, config.target.name, decision.agent,
                         "proposal_memory_patch", key, decision.proposal_json, evidence,
                         reward_hint, decision.model_response.schema_valid ? 0.7 : 0.2, now);
  append_line(summary.agent_memory_path,
              std::string("{\"run_id\":\"") + json_escape(summary.run_id) +
                  "\",\"agent\":\"" + json_escape(decision.agent) +
                  "\",\"key\":\"" + json_escape(key) +
                  "\",\"reward_hint\":" + std::to_string(reward_hint) +
                  ",\"proposal\":" +
                  json_value_or_raw(decision.proposal_json) + "}");
}

void persist_agent_decision(Database& db,
                            RunSummary& summary,
                            const AppConfig& config,
                            const AgentDecision& decision,
                            double reward_hint,
                            const std::filesystem::path& events_path) {
  db.insert_agent_decision(decision);
  ++summary.agent_decision_count;
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
  for (const auto& decision : decisions) {
    report << "- `" << decision.agent << "` provider=`" << decision.model_response.provider
           << "` schema_valid=`" << (decision.model_response.schema_valid ? "true" : "false")
           << "` context=`" << decision.model_response.context_hash << "`\n";
  }
  report << "\n## Micro Results\n\n";
  for (const auto& result : micro_results) {
    report << "- `" << result.intervention_id << "` campaign=`" << result.campaign_id
           << "` reward=`" << result.reward << "` new_paths=`" << result.new_paths
           << "` promoted=`" << (result.promoted ? "true" : "false") << "`\n";
  }
}

}  // namespace

RunSummary run_mvp(const RunOptions& options) {
  if (options.config_path.empty()) {
    throw std::runtime_error("RunOptions.config_path is required");
  }
  const auto loaded = load_config(options.config_path);
  const auto& config = loaded.config;

  RunSummary summary;
  summary.run_id = make_id("run");
  summary.main_campaign_id = "main_" + summary.run_id;
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
                env.os, env.arch, env.afl_version);
  db.insert_campaign(summary.main_campaign_id, summary.run_id, "main", "", "",
                     main_output_dir, now,
                     static_cast<uint64_t>(config.afl.main_budget_sec), "running");

  RecipeStore main_store(main_recipe_store);
  main_store.write_compact_recipes({make_default_dictionary_strategy({"FUZZ", "MAGIC", "TOKEN"})});
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
  PlateauDetector detector(plateau_config);

  std::vector<AflStats> main_samples;
  write_text_file(summary.coverage_csv_path,
                  "ts,execs_done,execs_per_sec,paths_total,bitmap_cvg,"
                  "unique_crashes,unique_hangs,recipe_hits,recipe_misses\n");
  for (const auto& stats_path : options.main_stats_paths) {
    auto stats = read_stats_or_throw(stats_path);
    main_samples.push_back(stats);
    db.insert_telemetry(summary.main_campaign_id, stats);
    ++summary.telemetry_count;
    append_line(summary.coverage_csv_path, coverage_csv_row(stats));
    append_line(events_path, telemetry_event_json(summary.run_id, summary.main_campaign_id, stats));
    const auto plateau = detector.add_sample(stats, summary.run_id, summary.main_campaign_id);
    if (plateau && summary.plateau_id.empty()) {
      summary.plateau_id = plateau->id;
      const auto blackboard = plateau_blackboard_json(*plateau, stats);
      db.insert_plateau(*plateau, blackboard);
      append_line(events_path, std::string("{\"event\":\"plateau_detected\",\"plateau\":") +
                                   plateau_event_json(*plateau) + "}");
    }
  }

  if (main_samples.empty()) {
    throw std::runtime_error("MVP run requires at least one --stats sample in the current implementation");
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
    db.insert_plateau(forced, plateau_blackboard_json(forced, main_samples.back()));
  }

  const auto snapshot_dir = summary.run_dir / "corpus_snapshot";
  const auto source_output = options.main_afl_output_dir.empty() ? config.target.input_dir
                                                                 : options.main_afl_output_dir;
  const auto snapshot = snapshot_corpus(source_output, snapshot_dir);
  append_line(events_path, std::string("{\"event\":\"corpus_snapshot\",\"snapshot\":") +
                               corpus_snapshot_json(snapshot) + "}");

  auto gateway = make_gateway(options);
  PlateauEvent blackboard_plateau;
  blackboard_plateau.id = summary.plateau_id;
  blackboard_plateau.run_id = summary.run_id;
  blackboard_plateau.campaign_id = summary.main_campaign_id;
  blackboard_plateau.detected_ts = static_cast<uint64_t>(main_samples.back().sampled_at);
  blackboard_plateau.window_sec = plateau_config.window_sec;
  blackboard_plateau.reason = "mvp_blackboard";
  const auto blackboard = plateau_blackboard_json(blackboard_plateau, main_samples.back());
  const auto tasks = make_default_agent_tasks(
      summary.plateau_id, blackboard, static_cast<uint32_t>(config.micro_campaign.budget_sec));
  auto decisions = run_agent_tasks(*gateway, summary.run_id, summary.plateau_id, tasks);
  for (const auto& decision : decisions) {
    persist_agent_decision(db, summary, config, decision, 0.0, events_path);
  }

  const auto specs = plan_micro_campaigns(
      config, summary.plateau_id, snapshot_dir, summary.run_dir / "micro", options.dry_run);
  prepare_micro_campaigns(specs);
  summary.micro_campaign_count = specs.size();

  std::vector<MicroResult> micro_results;
  const auto parent_stats = main_samples.back();
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const auto& spec = specs[i];
    db.insert_campaign(spec.id, summary.run_id, "micro", summary.main_campaign_id,
                       spec.intervention_id, spec.output_dir, now, spec.budget_sec,
                       options.dry_run ? "dry_run" : "planned");
    const auto stats_path = options.micro_stats_paths.empty()
                                ? options.main_stats_paths.back()
                                : options.micro_stats_paths[std::min(i, options.micro_stats_paths.size() - 1)];
    const auto micro_stats = read_stats_or_throw(stats_path);
    auto result = evaluate_micro_result(spec.intervention_id, spec.id, parent_stats, micro_stats);
    micro_results.push_back(result);
  }

  if (!micro_results.empty()) {
    auto winner_it = std::max_element(
        micro_results.begin(), micro_results.end(),
        [](const MicroResult& lhs, const MicroResult& rhs) { return lhs.reward < rhs.reward; });
    winner_it->promoted = true;
    summary.winner_intervention_id = winner_it->intervention_id;
    summary.winner_campaign_id = winner_it->campaign_id;
    summary.winner_reward = winner_it->reward;
  }

  for (const auto& result : micro_results) {
    db.insert_micro_result(result);
    db.finish_campaign(result.campaign_id, static_cast<uint64_t>(std::time(nullptr)), "completed");
    append_line(events_path, std::string("{\"event\":\"micro_result\",\"result\":") +
                                 micro_result_json(result) + "}");
  }

  AgentTask result_task;
  result_task.task_id = make_id("agent_task");
  result_task.agent_name = "ResultAnalysisAgent";
  result_task.objective =
      "Summarize validated micro-campaign rewards and produce agent memory patches";
  result_task.blackboard_slice_json =
      std::string("{\"plateau_id\":\"") + json_escape(summary.plateau_id) +
      "\",\"winner_intervention_id\":\"" +
      json_escape(summary.winner_intervention_id) + "\",\"winner_reward\":" +
      std::to_string(summary.winner_reward) + ",\"micro_results\":" +
      micro_results_json(micro_results) + "}";
  result_task.action_schema_json =
      "{\"allowed_actions\":[\"memory_patch\",\"priority_update\",\"keep_winner\"]}";
  result_task.output_schema_json =
      "{\"required\":[\"agent\",\"memory_patch\",\"critique\"]}";
  result_task.budget_sec = static_cast<uint32_t>(config.micro_campaign.budget_sec);
  const auto result_decisions = run_agent_tasks(
      *gateway, summary.run_id, summary.plateau_id, {result_task});
  for (const auto& decision : result_decisions) {
    persist_agent_decision(db, summary, config, decision, summary.winner_reward, events_path);
    decisions.push_back(decision);
  }

  auto promoted = make_default_dictionary_strategy(
      {"PROMOTED", summary.winner_intervention_id, "FUZZ", "TOKEN"});
  promoted.id = make_id("strategy_promoted");
  RecipeStore promoted_store(summary.run_dir / "promoted_recipes");
  summary.promoted_recipe_index = promoted_store.write_compact_recipes({promoted});
  append_line(events_path, std::string("{\"event\":\"promotion\",\"winner_intervention_id\":\"") +
                               json_escape(summary.winner_intervention_id) +
                               "\",\"recipe_index\":\"" +
                               json_escape(summary.promoted_recipe_index.string()) + "\"}");

  write_report(summary, config, main_samples, micro_results, decisions);

  const auto done = static_cast<uint64_t>(std::time(nullptr));
  db.finish_campaign(summary.main_campaign_id, done, "completed");
  db.finish_run(summary.run_id, done, "completed");
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
  out << "\"main_pid\":" << summary.main_pid;
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
