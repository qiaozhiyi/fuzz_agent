#include "fuzzpilot/experiments/m6_matrix.hpp"

#include "fuzzpilot/config.hpp"
#include "fuzzpilot/json_utils.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {
namespace {

struct M6Mode {
  std::string name;
  std::string purpose;
};

const std::vector<M6Mode>& m6_modes() {
  static const std::vector<M6Mode> modes = {
      {"baseline-afl", "plain AFL++ control without model agents, static analysis, or custom mutator"},
      {"rule-only", "deterministic fallback agents and micro-campaign validation"},
      {"no-static-analysis", "full loop without reverse-engineering/static-analysis intelligence"},
      {"no-mutator", "full loop without FuzzPilot custom mutator recipes"},
      {"no-microcampaign", "full agents but promote top-1 proposal directly without validation"},
      {"no-plateau", "agent invocation on fixed cadence, no plateau gating"},
      {"random-recipe", "recipes drawn from uniform random operators, agents disabled"},
      {"random-reward", "agents enabled but micro-campaign reward replaced with random noise"},
      {"edges-only", "full loop using edges-only reward (no crash/path bonus)"},
      {"full-agent", "complete agentic loop with configured model, mutator, memory, and static analysis"},
  };
  return modes;
}

std::string shell_quote(const std::string& value) {
  if (value.find_first_of(" \t'\"") == std::string::npos) {
    return value;
  }
  std::string out = "'";
  for (const char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
}

bool starts_with(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool is_loopback_http_endpoint(const std::string& endpoint) {
  return starts_with(endpoint, "http://127.0.0.1") ||
         starts_with(endpoint, "http://localhost") ||
         starts_with(endpoint, "http://[::1]");
}

std::vector<std::string> audit_config_security(const AppConfig& config) {
  std::vector<std::string> notes;
  if (starts_with(config.model_api.endpoint, "http://") &&
      !is_loopback_http_endpoint(config.model_api.endpoint)) {
    notes.push_back("model_api.endpoint uses plaintext HTTP outside loopback");
  }
  if (config.model_api.api_key_env.find("sk-") != std::string::npos ||
      config.model_api.api_key_env.find("token") != std::string::npos) {
    notes.push_back("model_api.api_key_env should be an environment variable name, not a secret value");
  }
  if (config.mutation_strategy.enabled &&
      config.mutation_strategy.custom_mutator_path.empty()) {
    notes.push_back("mutation_strategy is enabled but custom_mutator_path is empty");
  }
  if (config.static_analysis.enabled && config.static_analysis.timeout_sec <= 0) {
    notes.push_back("static_analysis.timeout_sec is disabled or invalid");
  }
  if (config.target.binary.is_absolute()) {
    notes.push_back("target.binary is absolute; relative paths are easier to reproduce across machines");
  }
  return notes;
}

std::string run_command(const std::filesystem::path& config_path,
                        const AppConfig& config,
                        const M6Mode& mode,
                        const M6MatrixOptions& options,
                        int repeat) {
  const auto run_work_dir = options.work_dir / config.target.name / mode.name /
                            ("r" + std::to_string(repeat));
  std::ostringstream out;
  out << "./build/fuzzpilot run";
  out << " --config " << shell_quote(config_path.string());
  out << " --work-dir " << shell_quote(run_work_dir.string());
  out << " --schema db/schema.sql";
  out << " --real-run";
  out << " --ablation " << mode.name;
  out << " --main-budget-sec " << options.main_budget_sec;
  out << " --micro-budget-sec " << options.micro_budget_sec;
  if (mode.name == "rule-only") {
    out << " --provider fake";
  }
  return out.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("failed to write M6 matrix file: " + path.string());
  }
  output << content;
}

}  // namespace

M6MatrixResult write_m6_matrix(const M6MatrixOptions& options) {
  if (options.config_paths.empty()) {
    throw std::runtime_error("M6 matrix requires at least one --config");
  }
  if (options.repeats <= 0) {
    throw std::runtime_error("M6 matrix --repeats must be positive");
  }
  if (options.main_budget_sec <= 0 || options.micro_budget_sec <= 0) {
    throw std::runtime_error("M6 matrix budgets must be positive");
  }

  M6MatrixResult result;
  result.manifest_path = options.out_dir / "m6_matrix.json";
  result.report_path = options.out_dir / "m6_matrix.md";
  result.target_count = options.config_paths.size();

  std::ostringstream manifest;
  std::ostringstream report;
  const auto created_ts = static_cast<uint64_t>(std::time(nullptr));

  manifest << "{";
  manifest << "\"created_ts\":" << created_ts << ",";
  manifest << "\"work_dir\":\"" << json_escape(options.work_dir.string()) << "\",";
  manifest << "\"repeats\":" << options.repeats << ",";
  manifest << "\"main_budget_sec\":" << options.main_budget_sec << ",";
  manifest << "\"micro_budget_sec\":" << options.micro_budget_sec << ",";
  manifest << "\"targets\":[";

  report << "# FuzzPilot M6 Experiment Matrix\n\n";
  report << "- targets: `" << options.config_paths.size() << "`\n";
  report << "- repeats per mode: `" << options.repeats << "`\n";
  report << "- main budget seconds: `" << options.main_budget_sec << "`\n";
  report << "- micro budget seconds: `" << options.micro_budget_sec << "`\n\n";

  bool first_target = true;
  for (const auto& config_path : options.config_paths) {
    const auto loaded = load_config(config_path);
    const auto& config = loaded.config;
    const auto errors = validate_config(config, options.check_runtime_paths);
    const auto security_notes = audit_config_security(config);
    result.error_count += errors.size();
    result.warning_count += loaded.warnings.size() + security_notes.size();

    if (!first_target) {
      manifest << ",";
    }
    first_target = false;
    manifest << "{";
    manifest << "\"config\":\"" << json_escape(config_path.string()) << "\",";
    manifest << "\"target\":\"" << json_escape(config.target.name) << "\",";
    manifest << "\"errors\":[";
    for (std::size_t i = 0; i < errors.size(); ++i) {
      if (i != 0) manifest << ",";
      manifest << "\"" << json_escape(errors[i]) << "\"";
    }
    manifest << "],\"warnings\":[";
    bool first_warning = true;
    for (const auto& warning : loaded.warnings) {
      if (!first_warning) manifest << ",";
      first_warning = false;
      manifest << "\"" << json_escape(warning) << "\"";
    }
    for (const auto& note : security_notes) {
      if (!first_warning) manifest << ",";
      first_warning = false;
      manifest << "\"" << json_escape(note) << "\"";
    }
    manifest << "],\"runs\":[";

    report << "## " << config.target.name << "\n\n";
    report << "- config: `" << config_path.string() << "`\n";
    report << "- binary: `" << config.target.binary.string() << "`\n";
    report << "- seeds: `" << config.target.input_dir.string() << "`\n";
    if (errors.empty() && loaded.warnings.empty() && security_notes.empty()) {
      report << "- audit: `clean`\n";
    }
    for (const auto& error : errors) {
      report << "- error: `" << error << "`\n";
    }
    for (const auto& warning : loaded.warnings) {
      report << "- warning: `" << warning << "`\n";
    }
    for (const auto& note : security_notes) {
      report << "- security note: `" << note << "`\n";
    }
    report << "\n";

    bool first_run = true;
    for (int repeat = 1; repeat <= options.repeats; ++repeat) {
      for (const auto& mode : m6_modes()) {
        const auto command = run_command(config_path, config, mode, options, repeat);
        const auto run_work_dir = options.work_dir / config.target.name / mode.name /
                                  ("r" + std::to_string(repeat));
        if (!first_run) manifest << ",";
        first_run = false;
        // Manifest entries now include result-state fields the runner is
        // expected to update after each run. This eliminates the previous
        // "have to grep logs to know what completed" problem and gives
        // the analysis scripts a single source of truth.
        manifest << "{";
        manifest << "\"mode\":\"" << json_escape(mode.name) << "\",";
        manifest << "\"repeat\":" << repeat << ",";
        manifest << "\"purpose\":\"" << json_escape(mode.purpose) << "\",";
        manifest << "\"command\":\"" << json_escape(command) << "\",";
        manifest << "\"work_dir\":\"" << json_escape(run_work_dir.string()) << "\",";
        manifest << "\"status\":\"planned\",";
        manifest << "\"started_ts\":0,";
        manifest << "\"completed_ts\":0,";
        manifest << "\"result_path\":\"\",";
        manifest << "\"summary\":{}";
        manifest << "}";
        report << "```bash\n" << command << "\n```\n\n";
        ++result.planned_runs;
      }
    }
    manifest << "]}";
  }

  manifest << "]}";

  write_text_file(result.manifest_path, manifest.str() + "\n");
  write_text_file(result.report_path, report.str());
  return result;
}

std::string m6_matrix_result_json(const M6MatrixResult& result) {
  std::ostringstream out;
  out << "{";
  out << "\"manifest_path\":\"" << json_escape(result.manifest_path.string()) << "\",";
  out << "\"report_path\":\"" << json_escape(result.report_path.string()) << "\",";
  out << "\"target_count\":" << result.target_count << ",";
  out << "\"planned_runs\":" << result.planned_runs << ",";
  out << "\"error_count\":" << result.error_count << ",";
  out << "\"warning_count\":" << result.warning_count;
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot
