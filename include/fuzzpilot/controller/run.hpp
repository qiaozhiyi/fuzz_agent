#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fuzzpilot {

struct RunOptions {
  std::filesystem::path config_path;
  std::filesystem::path work_dir = "work";
  std::filesystem::path db_path;
  std::filesystem::path schema_path = "db/schema.sql";
  std::filesystem::path main_afl_output_dir;
  std::vector<std::filesystem::path> main_stats_paths;
  std::vector<std::filesystem::path> micro_stats_paths;
  bool dry_run = true;
  std::string model_provider;
  std::string model_endpoint;
  std::string model_name;
  std::string api_key_env;
};

struct RunSummary {
  std::string run_id;
  std::string main_campaign_id;
  std::string plateau_id;
  std::string winner_intervention_id;
  std::string winner_campaign_id;
  double winner_reward = 0.0;
  std::filesystem::path run_dir;
  std::filesystem::path db_path;
  std::filesystem::path report_path;
  std::filesystem::path coverage_csv_path;
  std::filesystem::path agent_replay_log_path;
  std::filesystem::path agent_memory_path;
  std::filesystem::path main_launch_path;
  std::filesystem::path promoted_recipe_index;
  uint64_t telemetry_count = 0;
  uint64_t agent_decision_count = 0;
  uint64_t micro_campaign_count = 0;
  int main_pid = -1;
};

RunSummary run_mvp(const RunOptions& options);
std::string run_summary_json(const RunSummary& summary);

}  // namespace fuzzpilot
