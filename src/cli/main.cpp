#include "fuzzpilot/config.hpp"
#include "fuzzpilot/string_util.hpp"
#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/controller/run.hpp"
#include "fuzzpilot/env.hpp"
#include "fuzzpilot/interventions/intervention.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/micro/manager.hpp"
#include "fuzzpilot/model/gateway.hpp"
#include "fuzzpilot/mutation/recipe_store.hpp"
#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/runner/afl_runner.hpp"
#include "fuzzpilot/storage/db.hpp"
#include "fuzzpilot/telemetry/collector.hpp"
#include "fuzzpilot/telemetry/mutation_events.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kVersion = "0.1.0";

void usage() {
  std::cout
      << "fuzzpilot " << kVersion << "\n"
      << "commands:\n"
      << "  init [--root PATH]\n"
      << "  run --config PATH --stats PATH... [--afl-output-dir PATH] [--micro-stats PATH...] [--provider NAME]\n"
      << "  check-config --config PATH [--runtime]\n"
      << "  env --config PATH\n"
      << "  afl-command --config PATH --output-dir PATH [--recipe-store PATH]\n"
      << "  parse-stats --stats PATH\n"
      << "  parse-mutator-telemetry --mutator-telemetry PATH\n"
      << "  monitor-telemetry --campaign-dir PATH --db PATH --campaign-id ID --run-id ID\n"
      << "  replay-telemetry --stats PATH... --db PATH --campaign-id ID --run-id ID\n"
      << "  snapshot-corpus --afl-output-dir PATH --snapshot-dir PATH\n"
      << "  plan-micro-campaigns --config PATH --snapshot-dir PATH --work-dir PATH\n"
      << "  evaluate-micro --parent-stats PATH --micro-stats PATH --intervention-id ID --campaign-id ID\n"
      << "  run-model-agents --db PATH --run-id ID --plateau-id ID [--blackboard-json JSON]\n"
      << "  detect-plateau --older PATH --newer PATH [--window-sec N]\n"
      << "  init-db --db PATH [--schema PATH]\n"
      << "  write-default-recipe --recipe-store PATH\n"
      << "  write-m2-recipes --recipe-store PATH [--seed-id ID] [--seed-file PATH]\n"
      << "  lookup-recipe --recipe-store PATH [--seed-id ID] [--seed-file PATH]\n"
      << "  list-interventions [--budget-sec N]\n";
}

std::string arg_value(const std::vector<std::string>& args,
                      const std::string& name,
                      const std::string& fallback = {}) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) {
      return args[i + 1];
    }
  }
  return fallback;
}

bool has_arg(const std::vector<std::string>& args, const std::string& name) {
  for (const auto& arg : args) {
    if (arg == name) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> arg_values(const std::vector<std::string>& args,
                                    const std::string& name) {
  std::vector<std::string> values;
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) {
      values.push_back(args[i + 1]);
    }
  }
  return values;
}

void require_value(const std::string& value, const std::string& name) {
  if (value.empty()) {
    throw std::runtime_error("missing required argument: " + name);
  }
}


void append_text_line(const std::filesystem::path& path, const std::string& line) {
  if (path.empty()) {
    return;
  }
  std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  std::ofstream output(path, std::ios::app);
  if (!output) {
    throw std::runtime_error("failed to append: " + path.string());
  }
  output << line << "\n";
}

void ensure_coverage_csv_header(const std::filesystem::path& path) {
  if (path.empty() || std::filesystem::exists(path)) {
    return;
  }
  append_text_line(path,
                   "ts,campaign_id,execs_done,execs_per_sec,paths_total,bitmap_cvg,"
                   "unique_crashes,unique_hangs,recipe_hits,recipe_misses");
}

std::string coverage_csv_row(const std::string& campaign_id, const fuzzpilot::AflStats& stats) {
  return std::to_string(stats.sampled_at) + "," + campaign_id + "," +
         std::to_string(stats.execs_done) + "," + std::to_string(stats.execs_per_sec) + "," +
         std::to_string(stats.paths_total) + "," + std::to_string(stats.bitmap_cvg) + "," +
         std::to_string(stats.unique_crashes) + "," + std::to_string(stats.unique_hangs) + "," +
         std::to_string(stats.recipe_hits) + "," + std::to_string(stats.recipe_misses);
}

std::string telemetry_event_json(const std::string& run_id,
                                 const std::string& campaign_id,
                                 const fuzzpilot::AflStats& stats) {
  return std::string("{\"event\":\"telemetry_tick\",\"run_id\":\"") + fuzzpilot::json_escape(run_id) +
         "\",\"campaign_id\":\"" + fuzzpilot::json_escape(campaign_id) + "\",\"stats\":" +
         fuzzpilot::afl_stats_json(stats) + "}";
}

std::vector<std::string> token_args_or_default(const std::vector<std::string>& args) {
  auto tokens = arg_values(args, "--token");
  if (tokens.empty()) {
    tokens = {"FUZZ", "test", "IHDR", "IDAT", "IEND"};
  }
  return tokens;
}

std::string recipe_lookup_json(const fuzzpilot::RecipeLookupResult& result) {
  return std::string("{\"hit\":") + (result.hit ? "true" : "false") +
         ",\"selector_mode\":\"" + fuzzpilot::json_escape(result.selector_mode) +
         "\",\"selector_key\":\"" + fuzzpilot::json_escape(result.selector_key) +
         "\",\"priority\":" + std::to_string(result.priority) +
         ",\"strategy_id\":\"" + fuzzpilot::json_escape(result.strategy_id) +
         "\",\"recipe_path\":\"" + fuzzpilot::json_escape(result.recipe_path.string()) + "\"}";
}

void persist_sample(fuzzpilot::Database& db,
                    const std::string& run_id,
                    const std::string& campaign_id,
                    const fuzzpilot::AflStats& stats,
                    const std::filesystem::path& jsonl_path,
                    const std::filesystem::path& coverage_csv_path) {
  db.insert_telemetry(campaign_id, stats);
  append_text_line(jsonl_path, telemetry_event_json(run_id, campaign_id, stats));
  ensure_coverage_csv_header(coverage_csv_path);
  append_text_line(coverage_csv_path, coverage_csv_row(campaign_id, stats));
}

void persist_plateau(fuzzpilot::Database& db,
                     const fuzzpilot::PlateauEvent& event,
                     const fuzzpilot::AflStats& stats,
                     const std::filesystem::path& jsonl_path) {
  const auto blackboard = std::string("{\"main_metrics\":") +
                          fuzzpilot::afl_stats_json(stats) + "}";
  db.insert_plateau(event, blackboard);
  append_text_line(jsonl_path,
                   std::string("{\"event\":\"plateau_detected\",\"plateau\":") +
                       fuzzpilot::plateau_event_json(event) + "}");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
      usage();
      return args.empty() ? 1 : 0;
    }
    if (args[0] == "--version") {
      std::cout << "fuzzpilot " << kVersion << "\n";
      return 0;
    }

    const std::string command = args[0];
    if (command == "init") {
      const std::filesystem::path root = arg_value(args, "--root", ".");
      for (const auto& dir : {"configs/examples", "include/fuzzpilot", "src", "mutators/fuzzpilot",
                              "db", "experiments", "results", "work/recipes"}) {
        std::filesystem::create_directories(root / dir);
      }
      std::cout << "initialized fuzzpilot layout under " << root << "\n";
      return 0;
    }

    if (command == "run") {
      fuzzpilot::RunOptions options;
      options.config_path = arg_value(args, "--config");
      options.work_dir = arg_value(args, "--work-dir", "work");
      options.db_path = arg_value(args, "--db");
      options.schema_path = arg_value(args, "--schema", "db/schema.sql");
      options.main_afl_output_dir = arg_value(args, "--afl-output-dir");
      for (const auto& path : arg_values(args, "--stats")) {
        options.main_stats_paths.emplace_back(path);
      }
      for (const auto& path : arg_values(args, "--micro-stats")) {
        options.micro_stats_paths.emplace_back(path);
      }
      options.dry_run = !has_arg(args, "--real-run");
      options.model_provider = arg_value(args, "--provider");
      options.model_endpoint = arg_value(args, "--endpoint");
      options.model_name = arg_value(args, "--model");
      options.api_key_env = arg_value(args, "--api-key-env");
      require_value(options.config_path.string(), "--config");
      if (options.dry_run && options.main_stats_paths.empty()) {
        throw std::runtime_error("missing required argument: --stats (required in dry-run mode)");
      }
      const auto summary = fuzzpilot::run_mvp(options);
      std::cout << fuzzpilot::run_summary_json(summary) << "\n";
      return 0;
    }

    if (command == "check-config") {
      const auto config_path = arg_value(args, "--config");
      require_value(config_path, "--config");
      const auto loaded = fuzzpilot::load_config(config_path);
      for (const auto& warning : loaded.warnings) {
        std::cerr << "warning: " << warning << "\n";
      }
      const auto errors = fuzzpilot::validate_config(loaded.config, has_arg(args, "--runtime"));
      if (!errors.empty()) {
        for (const auto& error : errors) {
          std::cerr << "error: " << error << "\n";
        }
        return 2;
      }
      std::cout << fuzzpilot::summarize_config(loaded.config);
      return 0;
    }

    if (command == "env") {
      const auto config_path = arg_value(args, "--config");
      require_value(config_path, "--config");
      const auto loaded = fuzzpilot::load_config(config_path);
      const auto snapshot = fuzzpilot::capture_env_snapshot(loaded.config.afl.afl_fuzz.string());
      std::cout << fuzzpilot::env_snapshot_json(snapshot) << "\n";
      return 0;
    }

    if (command == "afl-command") {
      const auto config_path = arg_value(args, "--config");
      const auto output_dir = arg_value(args, "--output-dir");
      require_value(config_path, "--config");
      require_value(output_dir, "--output-dir");
      const auto loaded = fuzzpilot::load_config(config_path);
      const auto recipe_store = arg_value(args, "--recipe-store",
                                          loaded.config.mutation_strategy.recipe_store.string());
      const auto spec = fuzzpilot::build_main_afl_spec(loaded.config, output_dir, recipe_store);
      std::cout << fuzzpilot::shell_preview(spec) << "\n";
      return 0;
    }

    if (command == "parse-stats") {
      const auto stats_path = arg_value(args, "--stats");
      require_value(stats_path, "--stats");
      std::string error;
      const auto stats = fuzzpilot::parse_fuzzer_stats(stats_path, &error);
      if (!stats) {
        std::cerr << error << "\n";
        return 2;
      }
      std::cout << fuzzpilot::afl_stats_summary(*stats) << "\n";
      std::cout << fuzzpilot::afl_stats_json(*stats) << "\n";
      return 0;
    }

    if (command == "parse-mutator-telemetry") {
      const auto telemetry_path = arg_value(args, "--mutator-telemetry");
      require_value(telemetry_path, "--mutator-telemetry");
      std::string error;
      const auto snapshot = fuzzpilot::parse_mutator_telemetry(telemetry_path, &error);
      if (!snapshot) {
        std::cerr << error << "\n";
        return 2;
      }
      std::cout << fuzzpilot::mutation_telemetry_json(*snapshot) << "\n";
      return 0;
    }

    if (command == "replay-telemetry") {
      const auto db_path = arg_value(args, "--db");
      const auto campaign_id = arg_value(args, "--campaign-id");
      const auto run_id = arg_value(args, "--run-id");
      const auto schema_path = arg_value(args, "--schema", "db/schema.sql");
      const auto jsonl_path = arg_value(args, "--jsonl");
      const auto coverage_csv_path = arg_value(args, "--coverage-csv");
      const auto mutator_telemetry_path = arg_value(args, "--mutator-telemetry");
      const auto stats_paths = arg_values(args, "--stats");
      require_value(db_path, "--db");
      require_value(campaign_id, "--campaign-id");
      require_value(run_id, "--run-id");
      if (stats_paths.empty()) {
        throw std::runtime_error("missing required argument: --stats");
      }

      fuzzpilot::Database db;
      db.open(db_path);
      db.initialize_schema(schema_path);

      fuzzpilot::PlateauConfig plateau_config;
      plateau_config.window_sec = static_cast<uint64_t>(std::stoull(arg_value(args, "--window-sec", "600")));
      plateau_config.max_new_paths = 0;
      plateau_config.min_execs_delta = 1000;
      fuzzpilot::PlateauDetector detector(plateau_config);

      for (const auto& stats_path : stats_paths) {
        std::string error;
        auto stats = fuzzpilot::parse_fuzzer_stats(stats_path, &error);
        if (!stats) {
          std::cerr << error << "\n";
          return 2;
        }
        if (!mutator_telemetry_path.empty()) {
          const auto mutation = fuzzpilot::parse_mutator_telemetry(mutator_telemetry_path, &error);
          if (mutation) {
            stats->recipe_hits = mutation->recipe_hits;
            stats->recipe_misses = mutation->recipe_misses;
            stats->raw["recipe_hits"] = std::to_string(mutation->recipe_hits);
            stats->raw["recipe_misses"] = std::to_string(mutation->recipe_misses);
            stats->raw["mutation_count"] = std::to_string(mutation->mutation_count);
          }
        }
        persist_sample(db, run_id, campaign_id, *stats, jsonl_path, coverage_csv_path);
        const auto plateau = detector.add_sample(*stats, run_id, campaign_id);
        if (plateau) {
          persist_plateau(db, *plateau, *stats, jsonl_path);
          std::cout << fuzzpilot::plateau_event_json(*plateau) << "\n";
        } else {
          std::cout << fuzzpilot::afl_stats_summary(*stats) << "\n";
        }
      }
      return 0;
    }

    if (command == "monitor-telemetry") {
      const auto campaign_dir = arg_value(args, "--campaign-dir");
      const auto db_path = arg_value(args, "--db");
      const auto campaign_id = arg_value(args, "--campaign-id");
      const auto run_id = arg_value(args, "--run-id");
      const auto schema_path = arg_value(args, "--schema", "db/schema.sql");
      const auto jsonl_path = arg_value(args, "--jsonl");
      const auto coverage_csv_path = arg_value(args, "--coverage-csv");
      const auto mutator_telemetry_path = arg_value(args, "--mutator-telemetry");
      const int samples = std::stoi(arg_value(args, "--samples", "0"));
      const int interval_ms = std::stoi(arg_value(args, "--interval-ms", "10000"));
      require_value(campaign_dir, "--campaign-dir");
      require_value(db_path, "--db");
      require_value(campaign_id, "--campaign-id");
      require_value(run_id, "--run-id");

      fuzzpilot::Database db;
      db.open(db_path);
      db.initialize_schema(schema_path);
      fuzzpilot::TelemetryCollector collector(campaign_dir, mutator_telemetry_path);

      fuzzpilot::PlateauConfig plateau_config;
      plateau_config.window_sec = static_cast<uint64_t>(std::stoull(arg_value(args, "--window-sec", "600")));
      plateau_config.max_new_paths = 0;
      plateau_config.min_execs_delta = 1000;
      fuzzpilot::PlateauDetector detector(plateau_config);

      int collected = 0;
      while (samples <= 0 || collected < samples) {
        std::string error;
        const auto stats = collector.sample(&error);
        if (!stats) {
          std::cerr << error << "\n";
          return 2;
        }
        persist_sample(db, run_id, campaign_id, *stats, jsonl_path, coverage_csv_path);
        std::cout << fuzzpilot::afl_stats_summary(*stats) << "\n";
        const auto plateau = detector.add_sample(*stats, run_id, campaign_id);
        if (plateau) {
          persist_plateau(db, *plateau, *stats, jsonl_path);
          std::cout << fuzzpilot::plateau_event_json(*plateau) << "\n";
        }
        ++collected;
        if (samples <= 0 || collected < samples) {
          std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
      }
      return 0;
    }

    if (command == "snapshot-corpus") {
      const auto afl_output_dir = arg_value(args, "--afl-output-dir");
      const auto snapshot_dir = arg_value(args, "--snapshot-dir");
      require_value(afl_output_dir, "--afl-output-dir");
      require_value(snapshot_dir, "--snapshot-dir");
      const auto snapshot = fuzzpilot::snapshot_corpus(afl_output_dir, snapshot_dir);
      std::cout << fuzzpilot::corpus_snapshot_json(snapshot) << "\n";
      return 0;
    }

    if (command == "plan-micro-campaigns") {
      const auto config_path = arg_value(args, "--config");
      const auto snapshot_dir = arg_value(args, "--snapshot-dir");
      const auto work_dir = arg_value(args, "--work-dir");
      const auto plateau_id = arg_value(args, "--plateau-id", "plateau_smoke");
      require_value(config_path, "--config");
      require_value(snapshot_dir, "--snapshot-dir");
      require_value(work_dir, "--work-dir");
      const auto loaded = fuzzpilot::load_config(config_path);
      const bool dry_run = !has_arg(args, "--real-run");
      const auto specs = fuzzpilot::plan_micro_campaigns(
          loaded.config, plateau_id, snapshot_dir, work_dir, dry_run);
      fuzzpilot::prepare_micro_campaigns(specs);
      for (const auto& spec : specs) {
        std::cout << fuzzpilot::micro_campaign_spec_json(spec) << "\n";
      }
      return 0;
    }

    if (command == "evaluate-micro") {
      const auto parent_stats = arg_value(args, "--parent-stats");
      const auto micro_stats = arg_value(args, "--micro-stats");
      const auto intervention_id = arg_value(args, "--intervention-id");
      const auto campaign_id = arg_value(args, "--campaign-id");
      const auto db_path = arg_value(args, "--db");
      const auto schema_path = arg_value(args, "--schema", "db/schema.sql");
      require_value(parent_stats, "--parent-stats");
      require_value(micro_stats, "--micro-stats");
      require_value(intervention_id, "--intervention-id");
      require_value(campaign_id, "--campaign-id");
      std::string error;
      const auto parent = fuzzpilot::parse_fuzzer_stats(parent_stats, &error);
      if (!parent) {
        std::cerr << error << "\n";
        return 2;
      }
      const auto micro = fuzzpilot::parse_fuzzer_stats(micro_stats, &error);
      if (!micro) {
        std::cerr << error << "\n";
        return 2;
      }
      const auto result = fuzzpilot::evaluate_micro_result(
          intervention_id, campaign_id, *parent, *micro);
      if (!db_path.empty()) {
        fuzzpilot::Database db;
        db.open(db_path);
        db.initialize_schema(schema_path);
        db.insert_micro_result(result);
      }
      std::cout << fuzzpilot::micro_result_json(result) << "\n";
      return 0;
    }

    if (command == "run-model-agents") {
      const auto db_path = arg_value(args, "--db");
      const auto schema_path = arg_value(args, "--schema", "db/schema.sql");
      const auto run_id = arg_value(args, "--run-id");
      const auto plateau_id = arg_value(args, "--plateau-id");
      const auto blackboard_json = arg_value(args, "--blackboard-json", "{\"plateau\":{\"reason\":\"smoke\"}}");
      const auto provider = arg_value(args, "--provider", "fake");
      const auto endpoint = arg_value(args, "--endpoint", "http://127.0.0.1:11434/v1/chat/completions");
      const auto model = arg_value(args, "--model", "local-fuzzpilot-policy");
      const auto api_key_env = arg_value(args, "--api-key-env", "FUZZPILOT_MODEL_API_KEY");
      require_value(db_path, "--db");
      require_value(run_id, "--run-id");
      require_value(plateau_id, "--plateau-id");

      fuzzpilot::Database db;
      db.open(db_path);
      db.initialize_schema(schema_path);
      const auto tasks = fuzzpilot::make_default_agent_tasks(plateau_id, blackboard_json, 180);
      std::vector<fuzzpilot::AgentDecision> decisions;
      if (provider == "fake") {
        fuzzpilot::FakeModelGateway gateway;
        decisions = fuzzpilot::run_agent_tasks(gateway, run_id, plateau_id, tasks);
      } else if (provider == "openai-compatible") {
        fuzzpilot::OpenAICompatibleGateway gateway(endpoint, model, api_key_env, true);
        decisions = fuzzpilot::run_agent_tasks(gateway, run_id, plateau_id, tasks);
      } else {
        throw std::runtime_error("unsupported model provider: " + provider);
      }
      for (const auto& decision : decisions) {
        db.insert_agent_decision(decision);
        std::cout << fuzzpilot::agent_decision_json(decision) << "\n";
      }
      return 0;
    }

    if (command == "detect-plateau") {
      const auto older_path = arg_value(args, "--older");
      const auto newer_path = arg_value(args, "--newer");
      require_value(older_path, "--older");
      require_value(newer_path, "--newer");
      std::string error;
      const auto older = fuzzpilot::parse_fuzzer_stats(older_path, &error);
      if (!older) {
        std::cerr << error << "\n";
        return 2;
      }
      const auto newer = fuzzpilot::parse_fuzzer_stats(newer_path, &error);
      if (!newer) {
        std::cerr << error << "\n";
        return 2;
      }
      fuzzpilot::PlateauConfig plateau_config;
      plateau_config.window_sec = static_cast<uint64_t>(std::stoull(arg_value(args, "--window-sec", "600")));
      plateau_config.max_new_paths = 0;
      plateau_config.min_execs_delta = 1000;
      fuzzpilot::PlateauDetector detector(plateau_config);
      (void)detector.add_sample(*older, "run_smoke", "main_smoke");
      const auto event = detector.add_sample(*newer, "run_smoke", "main_smoke");
      if (!event) {
        std::cout << "{\"plateau\":false}\n";
        return 3;
      }
      std::cout << fuzzpilot::plateau_event_json(*event) << "\n";
      return 0;
    }

    if (command == "init-db") {
      const auto db_path = arg_value(args, "--db");
      const auto schema_path = arg_value(args, "--schema", "db/schema.sql");
      require_value(db_path, "--db");
      fuzzpilot::Database db;
      db.open(db_path);
      db.initialize_schema(schema_path);
      std::cout << "initialized sqlite database " << db_path << "\n";
      return 0;
    }

    if (command == "write-default-recipe") {
      const auto store_path = arg_value(args, "--recipe-store");
      require_value(store_path, "--recipe-store");
      auto strategy = fuzzpilot::make_default_dictionary_strategy({"FUZZ", "test", "IHDR", "IDAT", "IEND"});
      fuzzpilot::RecipeStore store(store_path);
      const auto json_path = store.write_strategy(strategy);
      const auto compact_path = store.write_global_mutator_recipe(strategy);
      std::cout << "wrote strategy " << json_path << "\n";
      std::cout << "wrote mutator recipe " << compact_path << "\n";
      return 0;
    }

    if (command == "write-m2-recipes") {
      const auto store_path = arg_value(args, "--recipe-store");
      require_value(store_path, "--recipe-store");
      const auto seed_id = arg_value(args, "--seed-id");
      const auto seed_file = arg_value(args, "--seed-file");
      std::vector<fuzzpilot::SeedMutationStrategy> strategies;
      strategies.push_back(fuzzpilot::make_default_dictionary_strategy(token_args_or_default(args)));
      if (!seed_id.empty()) {
        strategies.push_back(fuzzpilot::make_seed_focus_strategy(seed_id, token_args_or_default(args)));
      }
      if (!seed_file.empty()) {
        strategies.push_back(fuzzpilot::make_seed_hash_strategy(seed_file, token_args_or_default(args)));
      }
      fuzzpilot::RecipeStore store(store_path);
      const auto index = store.write_compact_recipes(strategies);
      std::cout << "wrote compact recipe index " << index << "\n";
      for (const auto& strategy : strategies) {
        std::cout << fuzzpilot::strategy_json(strategy) << "\n";
      }
      return 0;
    }

    if (command == "lookup-recipe") {
      const auto store_path = arg_value(args, "--recipe-store");
      const auto seed_id = arg_value(args, "--seed-id");
      const auto seed_file = arg_value(args, "--seed-file");
      require_value(store_path, "--recipe-store");
      std::string seed_hash;
      if (!seed_file.empty()) {
        seed_hash = fuzzpilot::stable_seed_hash_hex(seed_file);
      }
      fuzzpilot::RecipeStore store(store_path);
      const auto result = store.lookup_compact_recipe(seed_id, seed_hash);
      std::cout << recipe_lookup_json(result) << "\n";
      return result.hit ? 0 : 3;
    }

    if (command == "list-interventions") {
      const int budget = std::stoi(arg_value(args, "--budget-sec", "180"));
      for (const auto& intervention : fuzzpilot::default_v0_interventions(budget)) {
        std::cout << fuzzpilot::intervention_json(intervention) << "\n";
      }
      return 0;
    }

    std::cerr << "unknown command: " << command << "\n";
    usage();
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
