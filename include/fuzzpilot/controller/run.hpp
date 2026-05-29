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
  std::string ablation_mode = "full-agent";
  int main_budget_override_sec = 0;
  int micro_budget_override_sec = 0;
  int plateau_window_override_sec = 0;
  // Per-finding ablation hooks (see code review section "PAPER-BLOCKING").
  // These are orthogonal to ablation_mode so reviewers can request precise
  // factor-isolated experiments.
  bool disable_plateau_detector = false;
  bool disable_microcampaign = false;
  bool disable_static_analysis = false;
  std::string reward_mode = "edge_weighted";   // edge_weighted | edges_only | paths_only | random
  std::string recipe_source = "agent";          // agent | rule | random | none
  uint64_t micro_campaign_repeats = 1;          // statistical confidence per intervention
  uint64_t llm_token_budget_per_run = 0;        // 0 = unlimited; else hard-cap
  std::vector<std::string> disabled_agents;     // names like "Coordinator", "Mutator"
};

enum class WinnerStatus {
  kNoCandidates,   // no micro-campaigns were planned
  kAllFailed,      // every campaign failed to produce stats
  kSelected,       // at least one valid campaign; winner picked
  kNoSignificance, // best candidate not significantly better than baseline
};

const char* to_string(WinnerStatus status);

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
  uint64_t micro_campaigns_failed = 0;
  WinnerStatus winner_status = WinnerStatus::kNoCandidates;
  // Aggregate model accounting for the run (filled even on partial completion).
  uint64_t llm_calls = 0;
  uint64_t llm_input_tokens = 0;
  uint64_t llm_output_tokens = 0;
  uint64_t llm_failed_calls = 0;
  double llm_total_latency_ms = 0.0;
  int main_pid = -1;
  std::string ablation_mode;
  // P0.4: peak / cumulative aggregates over main_samples. The legacy
  // fields above reflect the LAST AFL state; if AFL crashed and the
  // controller re-launched, last-state under-reports the real progress.
  // These peaks survive crashes and are the honest paper numbers.
  uint64_t peak_bitmap_edges = 0;
  double peak_coverage_pct = 0.0;
  uint64_t cumulative_corpus_items = 0;
  int64_t total_main_runtime_sec = 0;
  // "loop_budget" == loop ran to completion normally;
  // "early_exit"  == AFL died and P0.3 broke the loop;
  // "signal_term" == SIGINT/SIGTERM from the runner.
  std::string main_afl_exit_reason;
};

RunSummary run_mvp(const RunOptions& options);
std::string run_summary_json(const RunSummary& summary);

}  // namespace fuzzpilot
