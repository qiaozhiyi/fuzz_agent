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
  // P0.2: cap AFL testcache to relieve OOM pressure when the custom
  // mutator + recipe cache are also competing for RSS. Only emit when
  // explicitly configured (> 0); -1/0 leaves AFL on its 50 MB default
  // so baseline runs without a TOML override stay byte-for-byte
  // compatible with prior reps.
  if (config.afl.testcache_size_mb > 0) {
    spec.env["AFL_TESTCACHE_SIZE"] =
        std::to_string(config.afl.testcache_size_mb);
  }
  if (config.mutation_strategy.enabled) {
    spec.env["AFL_CUSTOM_MUTATOR_LIBRARY"] =
        resolve_mutator_library_path(config.mutation_strategy.custom_mutator_path).string();
    spec.env["FUZZPILOT_RECIPE_STORE"] = recipe_store.string();
  }

  spec.argv.push_back(spec.afl_fuzz.string());
  // P0.5: explicit main fuzzer identity. Without -M/-S, AFL++ 4.x
  // auto-configures "-S default" (secondary, skipping deterministic
  // stages). For a single-fuzzer libxml2 run we want deterministic
  // bit-flip / arith stages, which only run in -M (master) mode.
  // The fixed name "default" also pins the stats path to
  // <output_dir>/default/fuzzer_stats so the TelemetryCollector
  // (see collector.cpp:find_fuzzer_stats_file) can locate it
  // deterministically rather than guessing via the fallback chain.
  spec.argv.push_back("-M");
  spec.argv.push_back("default");
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
  // P1.1: enable AFL++ CMPLOG when a cmplog-instrumented binary is
  // configured. AFL aborts with FATAL if -c points to a non-existent
  // file, so guard with an explicit exists() check producing a clear
  // error message rather than letting AFL die at fork-server startup.
  // Applies equally to baseline and full-agent (both call this
  // builder), so the SoK-grade CMPLOG comparison stays fair.
  if (!config.target.cmplog_binary.empty()) {
    if (!std::filesystem::exists(config.target.cmplog_binary)) {
      throw std::runtime_error(
          "cmplog_binary not found: " + config.target.cmplog_binary.string());
    }
    spec.argv.push_back("-c");
    spec.argv.push_back(config.target.cmplog_binary.string());
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
  // P0.2: same testcache cap as main. Micro instances are short-lived
  // but still benefit from reduced RSS when several launch in sequence.
  if (config.afl.testcache_size_mb > 0) {
    spec.env["AFL_TESTCACHE_SIZE"] =
        std::to_string(config.afl.testcache_size_mb);
  }
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
  // P1.1: same CMPLOG injection as main spec. Micro campaigns also
  // benefit from -c when probing past structured magic bytes in
  // short bursts.
  if (!config.target.cmplog_binary.empty()) {
    if (!std::filesystem::exists(config.target.cmplog_binary)) {
      throw std::runtime_error(
          "cmplog_binary not found: " + config.target.cmplog_binary.string());
    }
    spec.argv.push_back("-c");
    spec.argv.push_back(config.target.cmplog_binary.string());
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
