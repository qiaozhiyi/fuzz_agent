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

void Database::insert_run(const std::string& id,
                          const std::string& project,
                          const std::string& target_name,
                          uint64_t start_ts,
                          const std::string& status,
                          const std::string& os,
                          const std::string& arch,
                          const std::string& afl_version) {
  const char* sql =
      "INSERT OR REPLACE INTO runs (id, project, target_name, start_ts, end_ts, status, os, arch, "
      "afl_version, target_hash, seed_hash) VALUES (?, ?, ?, ?, NULL, ?, ?, ?, ?, '', '')";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, id);
  bind_text(stmt, 2, project);
  bind_text(stmt, 3, target_name);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(start_ts));
  bind_text(stmt, 5, status);
  bind_text(stmt, 6, os);
  bind_text(stmt, 7, arch);
  bind_text(stmt, 8, afl_version);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::finish_run(const std::string& id, uint64_t end_ts, const std::string& status) {
  const char* sql = "UPDATE runs SET end_ts = ?, status = ? WHERE id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(end_ts));
  bind_text(stmt, 2, status);
  bind_text(stmt, 3, id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::insert_campaign(const std::string& id,
                               const std::string& run_id,
                               const std::string& type,
                               const std::string& parent_campaign_id,
                               const std::string& intervention_id,
                               const std::filesystem::path& output_dir,
                               uint64_t start_ts,
                               uint64_t budget_sec,
                               const std::string& status) {
  const char* sql =
      "INSERT OR REPLACE INTO campaigns (id, run_id, type, parent_campaign_id, intervention_id, "
      "output_dir, start_ts, end_ts, budget_sec, status) VALUES (?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, id);
  bind_text(stmt, 2, run_id);
  bind_text(stmt, 3, type);
  bind_text(stmt, 4, parent_campaign_id);
  bind_text(stmt, 5, intervention_id);
  bind_text(stmt, 6, output_dir.string());
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(start_ts));
  sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(budget_sec));
  bind_text(stmt, 9, status);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

void Database::finish_campaign(const std::string& id, uint64_t end_ts, const std::string& status) {
  const char* sql = "UPDATE campaigns SET end_ts = ?, status = ? WHERE id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(end_ts));
  bind_text(stmt, 2, status);
  bind_text(stmt, 3, id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
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

void Database::insert_agent_memory(const std::string& id,
                                   const std::string& run_id,
                                   const std::string& target_name,
                                   const std::string& agent,
                                   const std::string& memory_type,
                                   const std::string& key,
                                   const std::string& value_json,
                                   const std::string& evidence_json,
                                   double reward,
                                   double confidence,
                                   uint64_t updated_ts) {
  const char* sql =
      "INSERT OR REPLACE INTO agent_memory (id, run_id, target_name, agent, memory_type, key, "
      "value_json, evidence_json, reward, confidence, updated_ts) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  bind_text(stmt, 1, id);
  bind_text(stmt, 2, run_id);
  bind_text(stmt, 3, target_name);
  bind_text(stmt, 4, agent);
  bind_text(stmt, 5, memory_type);
  bind_text(stmt, 6, key);
  bind_text(stmt, 7, value_json);
  bind_text(stmt, 8, evidence_json);
  sqlite3_bind_double(stmt, 9, reward);
  sqlite3_bind_double(stmt, 10, confidence);
  sqlite3_bind_int64(stmt, 11, static_cast<sqlite3_int64>(updated_ts));
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
}

std::vector<std::string> Database::get_recent_decisions(const std::string& run_id, int limit) {
  std::vector<std::string> decisions;
  const char* sql = "SELECT proposal_json FROM agent_decisions WHERE run_id = ? ORDER BY created_ts DESC LIMIT ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return decisions;
  }
  bind_text(stmt, 1, run_id);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    if (text) decisions.push_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(stmt);
  return decisions;
}

std::vector<std::string> Database::get_agent_memory(const std::string& run_id) {
  std::vector<std::string> memory;
  const char* sql = "SELECT value_json FROM agent_memory WHERE run_id = ? ORDER BY updated_ts DESC";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return memory;
  }
  bind_text(stmt, 1, run_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    if (text) memory.push_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(stmt);
  return memory;
}

void Database::close() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

}  // namespace fuzzpilot
