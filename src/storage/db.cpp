#include "fuzzpilot/storage/db.hpp"

#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sqlite3.h>

namespace fuzzpilot {
namespace {

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open SQL schema: " + path.string());
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

}  // namespace

Database::Database() = default;

Database::~Database() {
  close();
}

void Database::open(const std::filesystem::path& path) {
  close();
  std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
    std::string error = sqlite3_errmsg(db_);
    close();
    throw std::runtime_error("failed to open sqlite database: " + error);
  }
}

void Database::initialize_schema(const std::filesystem::path& schema_path) {
  execute(read_file(schema_path));
}

void Database::execute(const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error != nullptr ? error : "unknown sqlite error";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void Database::insert_telemetry(const std::string& campaign_id, const AflStats& stats) {
  const char* sql =
      "INSERT INTO telemetry (campaign_id, ts, execs_done, execs_per_sec, paths_total, "
      "unique_crashes, unique_hangs, bitmap_cvg, recipe_hits, recipe_misses, raw_json) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, campaign_id);
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(stats.sampled_at));
  sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(stats.execs_done));
  sqlite3_bind_double(stmt, 4, stats.execs_per_sec);
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(stats.paths_total));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(stats.unique_crashes));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(stats.unique_hangs));
  sqlite3_bind_double(stmt, 8, stats.bitmap_cvg);
  sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(stats.recipe_hits));
  sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(stats.recipe_misses));
  const auto raw = afl_stats_json(stats);
  bind_text(stmt, 11, raw);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::insert_plateau(const PlateauEvent& event, const std::string& blackboard_json) {
  const char* sql =
      "INSERT INTO plateaus (id, run_id, campaign_id, detected_ts, window_sec, execs_delta, "
      "new_paths_delta, reason, blackboard_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, event.id);
  bind_text(stmt, 2, event.run_id);
  bind_text(stmt, 3, event.campaign_id);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(event.detected_ts));
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(event.window_sec));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(event.execs_delta));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(event.new_paths_delta));
  bind_text(stmt, 8, event.reason);
  bind_text(stmt, 9, blackboard_json);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::insert_micro_result(const MicroResult& result) {
  const char* sql =
      "INSERT OR REPLACE INTO micro_results (id, intervention_id, strategy_ids_json, campaign_id, "
      "execs_done, new_paths, new_edges, unique_crashes, recipe_hits, recipe_misses, reward, promoted) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, result.id);
  bind_text(stmt, 2, result.intervention_id);
  bind_text(stmt, 3, "[]");
  bind_text(stmt, 4, result.campaign_id);
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(result.execs_done));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(result.new_paths));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(result.new_edges));
  sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(result.unique_crashes));
  sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(result.recipe_hits));
  sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(result.recipe_misses));
  sqlite3_bind_double(stmt, 11, result.reward);
  sqlite3_bind_int(stmt, 12, result.promoted ? 1 : 0);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::insert_agent_decision(const AgentDecision& decision) {
  const char* sql =
      "INSERT OR REPLACE INTO agent_decisions (id, run_id, plateau_id, agent, model_provider, "
      "model_name, task_json, context_hash, response_hash, latency_ms, schema_valid, fallback_used, "
      "error, proposal_json, created_ts) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, decision.id);
  bind_text(stmt, 2, decision.run_id);
  bind_text(stmt, 3, decision.plateau_id);
  bind_text(stmt, 4, decision.agent);
  bind_text(stmt, 5, decision.model_response.provider);
  bind_text(stmt, 6, decision.model_response.model);
  bind_text(stmt, 7, decision.task_json);
  bind_text(stmt, 8, decision.model_response.context_hash);
  bind_text(stmt, 9, decision.model_response.response_hash);
  sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(decision.model_response.latency_ms));
  sqlite3_bind_int(stmt, 11, decision.model_response.schema_valid ? 1 : 0);
  sqlite3_bind_int(stmt, 12, decision.fallback_used ? 1 : 0);
  bind_text(stmt, 13, decision.model_response.error);
  bind_text(stmt, 14, decision.proposal_json);
  sqlite3_bind_int64(stmt, 15, static_cast<sqlite3_int64>(decision.created_ts));
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::close() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

}  // namespace fuzzpilot
