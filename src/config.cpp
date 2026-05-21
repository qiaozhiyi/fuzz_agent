#include "fuzzpilot/config.hpp"
#include "fuzzpilot/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {
namespace {



bool is_valid_env_name(const std::string& value) {
  if (value.empty()) return false;
  const unsigned char first = static_cast<unsigned char>(value.front());
  if (!(std::isalpha(first) || value.front() == '_')) return false;
  return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_';
  });
}

bool should_check_filesystem_path(const std::filesystem::path& path) {
  return path.is_absolute() || path.has_parent_path();
}

std::string strip_comment(const std::string& line) {
  bool in_quote = false;
  char quote = '\0';
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if ((c == '"' || c == '\'') && (i == 0 || line[i - 1] != '\\')) {
      if (!in_quote) {
        in_quote = true;
        quote = c;
      } else if (quote == c) {
        in_quote = false;
      }
    }
    if (c == '#' && !in_quote) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::string unquote(std::string value) {
  value = trim(value);
  if (value.size() >= 2) {
    const char first = value.front();
    const char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      return value.substr(1, value.size() - 2);
    }
  }
  return value;
}

bool parse_bool(const std::string& raw) {
  std::string value = raw;
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value == "true" || value == "yes" || value == "1" || value == "on";
}

double parse_double(const std::string& raw, double fallback) {
  try {
    return std::stod(trim(raw));
  } catch (...) {
    return fallback;
  }
}

int parse_int(const std::string& raw, int fallback) {
  try {
    return std::stoi(trim(raw));
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> parse_inline_list(std::string value) {
  std::vector<std::string> result;
  value = trim(value);
  if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
    if (!value.empty()) {
      result.push_back(unquote(value));
    }
    return result;
  }
  value = value.substr(1, value.size() - 2);
  std::string current;
  bool in_quote = false;
  char quote = '\0';
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if ((c == '"' || c == '\'') && (i == 0 || value[i - 1] != '\\')) {
      if (!in_quote) {
        in_quote = true;
        quote = c;
      } else if (quote == c) {
        in_quote = false;
      }
    }
    if (c == ',' && !in_quote) {
      const auto item = unquote(current);
      if (!item.empty()) {
        result.push_back(item);
      }
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  const auto item = unquote(current);
  if (!item.empty()) {
    result.push_back(item);
  }
  return result;
}

std::size_t indent_of(const std::string& line) {
  std::size_t count = 0;
  while (count < line.size() && line[count] == ' ') {
    ++count;
  }
  return count;
}

void assign_root(AppConfig& config, const std::string& key, const std::string& value) {
  if (key == "project") {
    config.project = unquote(value);
  }
}

void assign_section(AppConfig& config,
                    const std::string& section,
                    const std::string& key,
                    const std::string& value) {
  if (section == "target") {
    if (key == "name") config.target.name = unquote(value);
    else if (key == "binary") config.target.binary = unquote(value);
    else if (key == "args") config.target.args = parse_inline_list(value);
    else if (key == "input_dir") config.target.input_dir = unquote(value);
    else if (key == "dict" || key == "dictionary") config.target.dict = unquote(value);
    else if (key == "cmplog_binary") config.target.cmplog_binary = unquote(value);
    else if (key == "custom_mutator") config.target.custom_mutator = unquote(value);
    else if (key == "timeout_ms") config.target.timeout_ms = parse_int(value, config.target.timeout_ms);
    else if (key == "memory_mb") config.target.memory_mb = parse_int(value, config.target.memory_mb);
    else if (key == "persistent") config.target.persistent = parse_bool(value);
    else if (key == "sanitizers") config.target.sanitizers = parse_inline_list(value);
  } else if (section == "afl") {
    if (key == "afl_fuzz") config.afl.afl_fuzz = unquote(value);
    else if (key == "afl_showmap") config.afl.afl_showmap = unquote(value);
    else if (key == "main_budget_sec") config.afl.main_budget_sec = parse_int(value, config.afl.main_budget_sec);
    else if (key == "plateau_window_sec") config.afl.plateau_window_sec = parse_int(value, config.afl.plateau_window_sec);
    else if (key == "plateau_min_new_edges") config.afl.plateau_min_new_edges = parse_int(value, config.afl.plateau_min_new_edges);
  } else if (section == "micro_campaign") {
    if (key == "enabled") config.micro_campaign.enabled = parse_bool(value);
    else if (key == "budget_sec") config.micro_campaign.budget_sec = parse_int(value, config.micro_campaign.budget_sec);
    else if (key == "max_parallel") config.micro_campaign.max_parallel = parse_int(value, config.micro_campaign.max_parallel);
    else if (key == "promote_metric") config.micro_campaign.promote_metric = unquote(value);
  } else if (section == "mutation_strategy") {
    if (key == "enabled") config.mutation_strategy.enabled = parse_bool(value);
    else if (key == "recipe_store") config.mutation_strategy.recipe_store = unquote(value);
    else if (key == "refresh_interval_sec") config.mutation_strategy.refresh_interval_sec = parse_int(value, config.mutation_strategy.refresh_interval_sec);
    else if (key == "default_policy") config.mutation_strategy.default_policy = unquote(value);
    else if (key == "max_recipes_per_seed") config.mutation_strategy.max_recipes_per_seed = parse_int(value, config.mutation_strategy.max_recipes_per_seed);
    else if (key == "recipe_ttl_sec") config.mutation_strategy.recipe_ttl_sec = parse_int(value, config.mutation_strategy.recipe_ttl_sec);
    else if (key == "custom_mutator_path") config.mutation_strategy.custom_mutator_path = unquote(value);
    else if (key == "hot_path_io") config.mutation_strategy.hot_path_io = unquote(value);
  } else if (section == "model_api" || section == "model") {
    if (key == "enabled") config.model_api.enabled = parse_bool(value);
    else if (key == "required_for_dynamic_decision") config.model_api.required_for_dynamic_decision = parse_bool(value);
    else if (key == "provider") config.model_api.provider = unquote(value);
    else if (key == "endpoint") config.model_api.endpoint = unquote(value);
    else if (key == "endpoint_env") config.model_api.endpoint_env = unquote(value);
    else if (key == "api_key_env") config.model_api.api_key_env = unquote(value);
    else if (key == "model" || key == "model_name") config.model_api.model = unquote(value);
    else if (key == "timeout_ms") config.model_api.timeout_ms = parse_int(value, config.model_api.timeout_ms);
    else if (key == "max_output_tokens") config.model_api.max_output_tokens = parse_int(value, config.model_api.max_output_tokens);
    else if (key == "temperature") config.model_api.temperature = parse_double(value, config.model_api.temperature);
    else if (key == "decision_interval_sec") config.model_api.decision_interval_sec = parse_int(value, config.model_api.decision_interval_sec);
    else if (key == "max_candidates_per_plateau") config.model_api.max_candidates_per_plateau = parse_int(value, config.model_api.max_candidates_per_plateau);
    else if (key == "require_schema_validation") config.model_api.require_schema_validation = parse_bool(value);
    else if (key == "record_prompts") config.model_api.record_prompts = parse_bool(value);
    else if (key == "replay_cache") config.model_api.replay_cache = unquote(value);
  } else if (section == "fuzzer") {
    if (key == "timeout_ms") config.target.timeout_ms = parse_int(value, config.target.timeout_ms);
  } else if (section == "plateau") {
    if (key == "window_sec") config.afl.plateau_window_sec = parse_int(value, config.afl.plateau_window_sec);
    else if (key == "threshold_paths") config.afl.plateau_min_new_edges = parse_int(value, config.afl.plateau_min_new_edges);
  } else if (section == "agent_runtime") {
    if (key == "strategic_mode") config.agent_runtime.strategic_mode = unquote(value);
    else if (key == "require_model_for_strategic_agents") config.agent_runtime.require_model_for_strategic_agents = parse_bool(value);
    else if (key == "rule_fallback_enabled") config.agent_runtime.rule_fallback_enabled = parse_bool(value);
    else if (key == "max_parallel_model_calls") config.agent_runtime.max_parallel_model_calls = parse_int(value, config.agent_runtime.max_parallel_model_calls);
    else if (key == "per_agent_timeout_ms") config.agent_runtime.per_agent_timeout_ms = parse_int(value, config.agent_runtime.per_agent_timeout_ms);
    else if (key == "agent_memory_store") config.agent_runtime.agent_memory_store = unquote(value);
    else if (key == "skill_store") config.agent_runtime.skill_store = unquote(value);
  } else if (section == "agents") {
    if (key == "scheduler") config.agents.scheduler = parse_bool(value);
    else if (key == "cmp") config.agents.cmp = parse_bool(value);
    else if (key == "dictionary") config.agents.dictionary = parse_bool(value);
    else if (key == "format") config.agents.format = parse_bool(value);
    else if (key == "corpus") config.agents.corpus = parse_bool(value);
    else if (key == "mutator") config.agents.mutator = parse_bool(value);
    else if (key == "mutation_policy") config.agents.mutation_policy = parse_bool(value);
    else if (key == "model_policy") config.agents.model_policy = parse_bool(value);
    else if (key == "model_format") config.agents.model_format = parse_bool(value);
    else if (key == "llm_format") config.agents.model_format = parse_bool(value);
  } else if (section == "static_analysis") {
    if (key == "enabled") config.static_analysis.enabled = parse_bool(value);
    else if (key == "backend" || key == "tool") config.static_analysis.backend = unquote(value);
    else if (key == "python_bin") config.static_analysis.python_bin = unquote(value);
    else if (key == "extractor_script") config.static_analysis.extractor_script = unquote(value);
    else if (key == "ida_dir") config.static_analysis.ida_dir = unquote(value);
    else if (key == "ghidra_home") config.static_analysis.ghidra_home = unquote(value);
    else if (key == "ghidra_headless" || key == "analyze_headless") config.static_analysis.ghidra_headless = unquote(value);
    else if (key == "timeout_sec") config.static_analysis.timeout_sec = parse_int(value, config.static_analysis.timeout_sec);
  }
}

void assign_nested(AppConfig& config,
                   const std::string& section,
                   const std::string& subsection,
                   const std::string& key,
                   const std::string& value) {
  if (section == "afl" && subsection == "base_env") {
    config.afl.base_env[key] = unquote(value);
  } else if (section == "mutation_strategy" && subsection == "agent_controls") {
    config.mutation_strategy.agent_controls[key] = parse_bool(value);
  } else if (section == "agent_runtime" && subsection == "model_agents") {
    config.agent_runtime.model_agents[key] = parse_bool(value);
  }
}

std::string join_args(const std::vector<std::string>& values) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) out << ", ";
    out << values[i];
  }
  return out.str();
}

#if !defined(__APPLE__)
bool looks_like_macho_binary(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  unsigned char bytes[4] = {0, 0, 0, 0};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (input.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
    return false;
  }
  const uint32_t magic = (static_cast<uint32_t>(bytes[0]) << 24) |
                         (static_cast<uint32_t>(bytes[1]) << 16) |
                         (static_cast<uint32_t>(bytes[2]) << 8) |
                         static_cast<uint32_t>(bytes[3]);
  switch (magic) {
    case 0xfeedface:
    case 0xfeedfacf:
    case 0xcafebabe:
    case 0xcafebabf:
    case 0xcefaedfe:
    case 0xcffaedfe:
    case 0xbebafeca:
    case 0xbfbafeca:
      return true;
    default:
      return false;
  }
}
#endif

}  // namespace

std::string normalize_static_backend(std::string value) {
  value = trim(value);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    if (c == '_') {
      return '-';
    }
    return static_cast<char>(std::tolower(c));
  });
  if (value.empty() || value == "off" || value == "disabled" || value == "false") {
    return "none";
  }
  if (value == "ghidra-headless") {
    return "ghidra";
  }
  if (value == "idalib") {
    return "ida";
  }
  return value;
}

std::filesystem::path resolve_ghidra_headless_path(const StaticAnalysisConfig& config) {
  if (!config.ghidra_home.empty()) {
    return config.ghidra_home / "support" / "analyzeHeadless";
  }
  return config.ghidra_headless.empty() ? std::filesystem::path("analyzeHeadless")
                                       : config.ghidra_headless;
}

std::filesystem::path resolve_mutator_library_path(const std::filesystem::path& configured_path) {
  if (configured_path.empty() || std::filesystem::exists(configured_path)) {
    return configured_path;
  }

  std::vector<std::string> suffixes;
#if defined(__APPLE__)
  suffixes = {".dylib", ".so"};
#elif defined(_WIN32)
  suffixes = {".dll"};
#else
  suffixes = {".so", ".dylib"};
#endif

  if (configured_path.extension().empty()) {
    for (const auto& suffix : suffixes) {
      auto candidate = configured_path;
      candidate += suffix;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  } else {
    for (const auto& suffix : suffixes) {
      auto candidate = configured_path;
      candidate.replace_extension(suffix);
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  }
  return configured_path;
}

ConfigLoadResult load_config(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open config: " + path.string());
  }

  ConfigLoadResult result;
  std::string section;
  std::string subsection;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    const auto without_comment = strip_comment(line);
    if (trim(without_comment).empty()) {
      continue;
    }

    const auto indent = indent_of(without_comment);
    const auto content = trim(without_comment);
    const auto colon = content.find(':');
    if (colon == std::string::npos) {
      result.warnings.push_back("ignored non key/value line " + std::to_string(line_no));
      continue;
    }

    // Performance optimization: Cast to std::string_view before substr
    // to avoid temporary heap allocations during the parsing loop.
    const auto key = trim(std::string_view(content).substr(0, colon));
    const auto value = trim(std::string_view(content).substr(colon + 1));

    if (indent == 0) {
      subsection.clear();
      if (value.empty()) {
        section = std::string(key);
      } else {
        section.clear();
        assign_root(result.config, std::string(key), std::string(value));
      }
    } else if (indent == 2) {
      if (value.empty()) {
        subsection = std::string(key);
      } else {
        assign_section(result.config, section, std::string(key), std::string(value));
      }
    } else if (indent >= 4) {
      assign_nested(result.config, section, subsection, std::string(key), std::string(value));
    }
  }

  result.config.afl.base_env.try_emplace("AFL_SKIP_CPUFREQ", "1");
  result.config.afl.base_env.try_emplace("AFL_NO_UI", "1");
  return result;
}

std::vector<std::string> validate_config(const AppConfig& config, bool check_runtime_paths) {
  std::vector<std::string> errors;
  if (config.project.empty()) {
    errors.push_back("project is required");
  }
  if (config.target.name.empty()) {
    errors.push_back("target.name is required");
  }
  if (config.target.binary.empty()) {
    errors.push_back("target.binary is required");
  }
  if (config.target.input_dir.empty()) {
    errors.push_back("target.input_dir is required");
  }
  // Sensitive env vars that fuzzpilot itself manages; allowing the
  // config to override them would let a malicious config redirect the
  // custom mutator, the recipe store, or LD_PRELOAD into attacker-
  // controlled paths. Validation rejects them outright with a clear
  // error rather than silently letting the override win.
  static const std::set<std::string> kReservedEnvKeys = {
      "AFL_CUSTOM_MUTATOR_LIBRARY",
      "AFL_PRELOAD",
      "LD_PRELOAD",
      "DYLD_INSERT_LIBRARIES",   // macOS equivalent of LD_PRELOAD
      "FUZZPILOT_RECIPE_STORE",
      "FUZZPILOT_MUTATOR_TELEMETRY",
      "FUZZPILOT_MUTATOR_DEBUG",
      "PATH",
      "LD_LIBRARY_PATH",
  };
  for (const auto& [key, value] : config.afl.base_env) {
    (void)value;
    if (!is_valid_env_name(key)) {
      errors.push_back("afl.base_env contains invalid environment variable name: " + key);
    }
    if (kReservedEnvKeys.count(key)) {
      errors.push_back("afl.base_env may not override reserved variable: " + key);
    }
  }
  if (!config.model_api.api_key_env.empty() && !is_valid_env_name(config.model_api.api_key_env)) {
    errors.push_back("model_api.api_key_env is not a valid environment variable name: " +
                     config.model_api.api_key_env);
  }
  if (!config.model_api.endpoint_env.empty() && !is_valid_env_name(config.model_api.endpoint_env)) {
    errors.push_back("model_api.endpoint_env is not a valid environment variable name: " +
                     config.model_api.endpoint_env);
  }
  if (config.micro_campaign.enabled &&
      config.micro_campaign.budget_sec >= config.afl.main_budget_sec) {
    errors.push_back("micro_campaign.budget_sec must be smaller than afl.main_budget_sec");
  }
  if (config.afl.main_budget_sec <= 0) {
    errors.push_back("afl.main_budget_sec must be positive");
  }
  if (config.afl.plateau_window_sec <= 0) {
    errors.push_back("afl.plateau_window_sec must be positive");
  }
  if (config.micro_campaign.budget_sec <= 0) {
    errors.push_back("micro_campaign.budget_sec must be positive");
  }
  if (config.target.timeout_ms <= 0) {
    errors.push_back("target.timeout_ms must be positive");
  }
  if (config.target.memory_mb < 0) {
    errors.push_back("target.memory_mb must be non-negative");
  }
  if (config.model_api.enabled) {
    if (config.model_api.model.empty()) {
      errors.push_back("model_api.model is required when model_api.enabled is true");
    }
    if (config.model_api.endpoint.empty() && config.model_api.endpoint_env.empty()) {
      errors.push_back("model_api.endpoint or model_api.endpoint_env is required when model_api.enabled is true");
    }
    if (config.model_api.timeout_ms <= 0) {
      errors.push_back("model_api.timeout_ms must be positive");
    }
    if (config.model_api.max_output_tokens <= 0) {
      errors.push_back("model_api.max_output_tokens must be positive");
    }
    if (config.model_api.max_candidates_per_plateau <= 0) {
      errors.push_back("model_api.max_candidates_per_plateau must be positive");
    }
  }
  if (config.agent_runtime.strategic_mode == "model_api_required") {
    if (!config.model_api.enabled) {
      errors.push_back("model_api.enabled must be true when agent_runtime.strategic_mode is model_api_required");
    }
    if (config.agent_runtime.max_parallel_model_calls <= 0) {
      errors.push_back("agent_runtime.max_parallel_model_calls must be positive");
    }
    if (config.agent_runtime.per_agent_timeout_ms <= 0) {
      errors.push_back("agent_runtime.per_agent_timeout_ms must be positive");
    }
  }
  if (check_runtime_paths) {
    if (!std::filesystem::exists(config.target.binary)) {
      errors.push_back("target.binary does not exist: " + config.target.binary.string());
    } else if ((std::filesystem::status(config.target.binary).permissions() &
                std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
      errors.push_back("target.binary is not owner-executable: " + config.target.binary.string());
#if !defined(__APPLE__)
    } else if (looks_like_macho_binary(config.target.binary)) {
      errors.push_back("target.binary is a Mach-O binary; rebuild the target for Ubuntu/Linux: " +
                       config.target.binary.string());
#endif
    }
    if (!std::filesystem::is_directory(config.target.input_dir)) {
      errors.push_back("target.input_dir does not exist: " + config.target.input_dir.string());
    } else if (std::filesystem::is_empty(config.target.input_dir)) {
      errors.push_back("target.input_dir is empty: " + config.target.input_dir.string());
    }
    if (config.mutation_strategy.enabled &&
        !std::filesystem::exists(resolve_mutator_library_path(config.mutation_strategy.custom_mutator_path))) {
      errors.push_back("mutation_strategy.custom_mutator_path does not exist: " +
                       config.mutation_strategy.custom_mutator_path.string());
    }
    if (config.static_analysis.enabled) {
      const auto backend = normalize_static_backend(config.static_analysis.backend);
      if (backend == "none") {
        errors.push_back("static_analysis.enabled is true but static_analysis.backend is disabled");
      } else if (backend != "ida" && backend != "ghidra") {
        errors.push_back("static_analysis.backend is unsupported: " + config.static_analysis.backend);
      }
      if (!std::filesystem::exists(config.static_analysis.extractor_script)) {
        errors.push_back("static_analysis.extractor_script does not exist: " +
                         config.static_analysis.extractor_script.string());
      }
      if (backend == "ida") {
        if (should_check_filesystem_path(config.static_analysis.python_bin) &&
            !std::filesystem::exists(config.static_analysis.python_bin)) {
          errors.push_back("static_analysis.python_bin does not exist: " +
                           config.static_analysis.python_bin.string());
        }
        if (config.static_analysis.ida_dir.empty() ||
            !std::filesystem::is_directory(config.static_analysis.ida_dir)) {
          errors.push_back("static_analysis.ida_dir does not exist: " +
                           config.static_analysis.ida_dir.string());
        }
      } else if (backend == "ghidra") {
        const auto headless = resolve_ghidra_headless_path(config.static_analysis);
        if (should_check_filesystem_path(headless) && !std::filesystem::exists(headless)) {
          errors.push_back("static_analysis.ghidra_headless does not exist: " +
                           headless.string());
        }
      }
      if (config.static_analysis.timeout_sec <= 0) {
        errors.push_back("static_analysis.timeout_sec must be positive");
      }
    }
  }
  return errors;
}

std::string summarize_config(const AppConfig& config) {
  std::ostringstream out;
  out << "project=" << config.project << "\n";
  out << "target=" << config.target.name << "\n";
  out << "binary=" << config.target.binary.string() << "\n";
  out << "args=[" << join_args(config.target.args) << "]\n";
  out << "input_dir=" << config.target.input_dir.string() << "\n";
  out << "afl_fuzz=" << config.afl.afl_fuzz.string() << "\n";
  out << "main_budget_sec=" << config.afl.main_budget_sec << "\n";
  out << "plateau_window_sec=" << config.afl.plateau_window_sec << "\n";
  out << "micro_budget_sec=" << config.micro_campaign.budget_sec << "\n";
  out << "recipe_store=" << config.mutation_strategy.recipe_store.string() << "\n";
  out << "model_api_enabled=" << (config.model_api.enabled ? "true" : "false") << "\n";
  out << "model_provider=" << config.model_api.provider << "\n";
  out << "model=" << config.model_api.model << "\n";
  out << "model_endpoint=" << config.model_api.endpoint << "\n";
  out << "model_required_for_dynamic_decision="
      << (config.model_api.required_for_dynamic_decision ? "true" : "false") << "\n";
  out << "agent_strategic_mode=" << config.agent_runtime.strategic_mode << "\n";
  out << "agent_require_model="
      << (config.agent_runtime.require_model_for_strategic_agents ? "true" : "false") << "\n";
  out << "agent_max_parallel_model_calls="
      << config.agent_runtime.max_parallel_model_calls << "\n";
  out << "agent_memory_store=" << config.agent_runtime.agent_memory_store.string() << "\n";
  out << "static_analysis_enabled="
      << (config.static_analysis.enabled ? "true" : "false") << "\n";
  out << "static_analysis_backend="
      << normalize_static_backend(config.static_analysis.backend) << "\n";
  return out.str();
}

}  // namespace fuzzpilot
