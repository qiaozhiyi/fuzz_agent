#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fuzzpilot {

struct TargetConfig {
  std::string name;
  std::filesystem::path binary;
  std::vector<std::string> args;
  std::filesystem::path input_dir;
  std::filesystem::path dict;
  std::filesystem::path cmplog_binary;
  std::filesystem::path custom_mutator;
  int timeout_ms = 1000;
  int memory_mb = 1024;
  bool persistent = false;
  std::vector<std::string> sanitizers;
};

struct AflConfig {
  std::filesystem::path afl_fuzz = "afl-fuzz";
  std::filesystem::path afl_showmap = "afl-showmap";
  std::map<std::string, std::string> base_env;
  int main_budget_sec = 3600;
  int plateau_window_sec = 600;
  int plateau_min_new_edges = 0;
};

struct MicroCampaignConfig {
  bool enabled = true;
  int budget_sec = 180;
  int max_parallel = 1;
  std::string promote_metric = "new_edges";
};

struct MutationStrategyConfig {
  bool enabled = true;
  std::filesystem::path recipe_store = "./work/recipes";
  int refresh_interval_sec = 15;
  std::string default_policy = "afl_fallback";
  int max_recipes_per_seed = 4;
  int recipe_ttl_sec = 900;
  std::filesystem::path custom_mutator_path = "./build-make/mutators/fuzzpilot/libfuzzpilot_mutator.dylib";
  std::string hot_path_io = "mmap";
  std::map<std::string, bool> agent_controls;
};

struct ModelApiConfig {
  bool enabled = true;
  bool required_for_dynamic_decision = true;
  std::string provider = "openai_compatible";
  std::string endpoint = "http://127.0.0.1:11434/v1/chat/completions";
  std::string endpoint_env = "FUZZPILOT_MODEL_ENDPOINT";
  std::string api_key_env = "FUZZPILOT_MODEL_API_KEY";
  std::string model = "local-fuzzpilot-policy";
  int timeout_ms = 30000;
  int max_output_tokens = 2048;
  double temperature = 0.2;
  int decision_interval_sec = 120;
  int max_candidates_per_plateau = 6;
  bool require_schema_validation = true;
  bool record_prompts = true;
  std::filesystem::path replay_cache = "./work/agent_decisions";
};

struct AgentRuntimeConfig {
  std::string strategic_mode = "model_api_required";
  bool require_model_for_strategic_agents = true;
  bool rule_fallback_enabled = true;
  int max_parallel_model_calls = 2;
  int per_agent_timeout_ms = 30000;
  std::filesystem::path agent_memory_store = "./work/agent_memory";
  std::filesystem::path skill_store = "./work/agent_skills";
  std::map<std::string, bool> model_agents;
};

struct AgentConfig {
  bool scheduler = true;
  bool cmp = true;
  bool dictionary = true;
  bool format = true;
  bool corpus = true;
  bool mutator = true;
  bool mutation_policy = true;
  bool model_policy = true;
  bool model_format = true;
};

struct StaticAnalysisConfig {
  bool enabled = false;
  std::filesystem::path python_bin = "python3";
  std::filesystem::path extractor_script = "./scripts/ida_extractor.py";
  std::filesystem::path ida_dir;  // IDADIR environment variable value
  int timeout_sec = 60;
};

struct AppConfig {
  std::string project = "fuzzpilot";
  TargetConfig target;
  AflConfig afl;
  MicroCampaignConfig micro_campaign;
  MutationStrategyConfig mutation_strategy;
  ModelApiConfig model_api;
  AgentRuntimeConfig agent_runtime;
  AgentConfig agents;
  StaticAnalysisConfig static_analysis;
};

struct ConfigLoadResult {
  AppConfig config;
  std::vector<std::string> warnings;
};

ConfigLoadResult load_config(const std::filesystem::path& path);
std::vector<std::string> validate_config(const AppConfig& config, bool check_runtime_paths);
std::string summarize_config(const AppConfig& config);

}  // namespace fuzzpilot
