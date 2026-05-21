#include "fuzzpilot/runner/afl_runner.hpp"

#include <sstream>

namespace fuzzpilot {
namespace {

std::string quote_for_preview(const std::string& value) {
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

}  // namespace

AflLaunchSpec build_main_afl_spec(const AppConfig& config,
                                  const std::filesystem::path& output_dir,
                                  const std::filesystem::path& recipe_store) {
  AflLaunchSpec spec;
  spec.afl_fuzz = config.afl.afl_fuzz;
  spec.output_dir = output_dir;
  spec.env = config.afl.base_env;
  if (config.mutation_strategy.enabled) {
    spec.env["AFL_CUSTOM_MUTATOR_LIBRARY"] =
        resolve_mutator_library_path(config.mutation_strategy.custom_mutator_path).string();
    spec.env["FUZZPILOT_RECIPE_STORE"] = recipe_store.string();
  }

  spec.argv.push_back(spec.afl_fuzz.string());
  spec.argv.push_back("-i");
  spec.argv.push_back(config.target.input_dir.string());
  spec.argv.push_back("-o");
  spec.argv.push_back(output_dir.string());
  spec.argv.push_back("-m");
  spec.argv.push_back(std::to_string(config.target.memory_mb));
  spec.argv.push_back("-t");
  spec.argv.push_back(std::to_string(config.target.timeout_ms));
  if (!config.target.dict.empty()) {
    spec.argv.push_back("-x");
    spec.argv.push_back(config.target.dict.string());
  }
  spec.argv.push_back("--");
  spec.argv.push_back(config.target.binary.string());
  for (const auto& arg : config.target.args) {
    spec.argv.push_back(arg);
  }
  return spec;
}

AflLaunchSpec build_micro_afl_spec(const AppConfig& config,
                                   const MicroCampaignSpec& micro_spec,
                                   const std::filesystem::path& dict_override) {
  AflLaunchSpec spec;
  spec.afl_fuzz = config.afl.afl_fuzz;
  spec.output_dir = micro_spec.output_dir;
  spec.env = config.afl.base_env;
  if (config.mutation_strategy.enabled) {
    spec.env["AFL_CUSTOM_MUTATOR_LIBRARY"] =
        resolve_mutator_library_path(config.mutation_strategy.custom_mutator_path).string();
    spec.env["FUZZPILOT_RECIPE_STORE"] = micro_spec.recipe_store.string();
  }

  spec.argv.push_back(spec.afl_fuzz.string());
  spec.argv.push_back("-i");
  spec.argv.push_back(micro_spec.input_dir.string());
  spec.argv.push_back("-o");
  spec.argv.push_back(micro_spec.output_dir.string());
  spec.argv.push_back("-m");
  spec.argv.push_back(std::to_string(config.target.memory_mb));
  spec.argv.push_back("-t");
  spec.argv.push_back(std::to_string(config.target.timeout_ms));
  // Use the static-analysis-generated dict if available, otherwise fall back to the config dict.
  const auto& effective_dict = (!dict_override.empty() && std::filesystem::exists(dict_override))
                                   ? dict_override
                                   : config.target.dict;
  if (!effective_dict.empty()) {
    spec.argv.push_back("-x");
    spec.argv.push_back(effective_dict.string());
  }
  spec.argv.push_back("-V");
  spec.argv.push_back(std::to_string(micro_spec.budget_sec));
  spec.argv.push_back("--");
  spec.argv.push_back(config.target.binary.string());
  for (const auto& arg : config.target.args) {
    spec.argv.push_back(arg);
  }
  return spec;
}

std::string shell_preview(const AflLaunchSpec& spec) {
  std::ostringstream out;
  for (const auto& [key, value] : spec.env) {
    out << key << "=" << quote_for_preview(value) << " ";
  }
  for (std::size_t i = 0; i < spec.argv.size(); ++i) {
    if (i != 0) {
      out << " ";
    }
    out << quote_for_preview(spec.argv[i]);
  }
  return out.str();
}

}  // namespace fuzzpilot
