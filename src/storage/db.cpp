#include "fuzzpilot/storage/db.hpp"

#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <cstdio>
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
  // SQLITE_TRANSIENT makes SQLite copy the string immediately, so the
  // caller doesn't need to keep `value` alive past the bind call. This
  // is the safe default; SQLITE_STATIC requires guaranteed lifetime.
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

// Helper for inserts that share the same finalize-and-throw pattern.
// `stmt` must already be bound. Throws if step doesn't return DONE.
void step_or_throw(sqlite3* db, sqlite3_stmt* stmt, const char* context) {
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    std::string error = sqlite3_errmsg(db);
    // Best-effort reset so the cached statement isn't poisoned. Failure
    // to reset is non-fatal — the next caller will reset again.
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    throw std::runtime_error(std::string(context) + ": " + error);
  }
  // Reset for reuse. clear_bindings is important because bound text
  // values still reference the previous call's strings (we used
  // SQLITE_TRANSIENT so they're copied, but clearing keeps the slot
  // clean for any later partial binds).
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
}

}  // namespace

Database::Database() = default;

Database::~Database() {
  // Important: finalize cached statements BEFORE sqlite3_close, or
  // close() returns SQLITE_BUSY and leaks the connection. close()
  // does this in the right order; we just delegate.
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
  apply_runtime_pragmas();
}

void Database::apply_runtime_pragmas() {
  // These pragmas re-apply on every open (some are connection-scoped,
  // not database-scoped). Failure is logged but non-fatal so a missing
  // PRAGMA permission doesn't kill the run.
  const char* pragmas =
      "PRAGMA journal_mode=WAL;"
      "PRAGMA synchronous=NORMAL;"
      "PRAGMA busy_timeout=5000;"
      "PRAGMA temp_store=MEMORY;";
  char* err = nullptr;
  if (sqlite3_exec(db_, pragmas, nullptr, nullptr, &err) != SQLITE_OK) {
    std::fprintf(stderr, "[db] pragma apply warning: %s\n", err ? err : "?");
    sqlite3_free(err);
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

void Database::begin() {
  // Nest counter so callers can compose batches without checking each
  // other. Only emit BEGIN when transitioning 0 -> 1.
  if (txn_depth_++ == 0) {
    char* err = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &err) != SQLITE_OK) {
      std::string msg = err ? err : "BEGIN failed";
      sqlite3_free(err);
      // Undo the increment so the depth tracks reality.
      txn_depth_ = 0;
      throw std::runtime_error(msg);
    }
  }
}

void Database::commit() {
  if (txn_depth_ == 0) {
    // Caller bug, but don't blow up — log and ignore.
    std::fprintf(stderr, "[db] commit called outside transaction\n");
    return;
  }
  if (--txn_depth_ == 0) {
    char* err = nullptr;
    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err) != SQLITE_OK) {
      std::string msg = err ? err : "COMMIT failed";
      sqlite3_free(err);
      throw std::runtime_error(msg);
    }
  }
}

void Database::rollback() {
  if (txn_depth_ == 0) {
    return;
  }
  txn_depth_ = 0;
  char* err = nullptr;
  if (sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err) != SQLITE_OK) {
    std::fprintf(stderr, "[db] rollback warning: %s\n", err ? err : "?");
    sqlite3_free(err);
  }
}

sqlite3_stmt* Database::prepared(sqlite3_stmt** slot, const char* sql) {
  if (*slot != nullptr) {
    return *slot;
  }
  if (sqlite3_prepare_v2(db_, sql, -1, slot, nullptr) != SQLITE_OK) {
    std::string err = sqlite3_errmsg(db_);
    *slot = nullptr;
    throw std::runtime_error(std::string("prepare failed: ") + err);
  }
  return *slot;
}

void Database::finalize_all() {
  auto fin = [](sqlite3_stmt*& s) {
    if (s) { sqlite3_finalize(s); s = nullptr; }
  };
  fin(stmt_insert_run_);
  fin(stmt_finish_run_);
  fin(stmt_insert_campaign_);
  fin(stmt_finish_campaign_);
  fin(stmt_insert_telemetry_);
  fin(stmt_insert_plateau_);
  fin(stmt_insert_micro_result_);
  fin(stmt_insert_agent_decision_);
  fin(stmt_insert_agent_memory_);
  fin(stmt_get_recent_decisions_);
  fin(stmt_get_agent_memory_);
}

void Database::insert_run(const std::string& id,
                          const std::string& project,
                          const std::string& target_name,
                          uint64_t start_ts,
                          const std::string& status,
                          const std::string& os,
                          const std::string& arch,
                          const std::string& afl_version,
                          const std::string& ablation_mode) {
  // winner_status / llm_* default to NULL/0 via DEFAULT in schema; we
  // explicitly set end_ts=NULL since the run is just starting.
  static constexpr const char* kSql =
      "INSERT OR REPLACE INTO runs (id, project, target_name, start_ts, end_ts, status, os, arch, "
      "afl_version, target_hash, seed_hash, ablation_mode) "
      "VALUES (?, ?, ?, ?, NULL, ?, ?, ?, ?, '', '', ?)";
  auto* stmt = prepared(&stmt_insert_run_, kSql);
  bind_text(stmt, 1, id);
  bind_text(stmt, 2, project);
  bind_text(stmt, 3, target_name);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(start_ts));
  bind_text(stmt, 5, status);
  bind_text(stmt, 6, os);
  bind_text(stmt, 7, arch);
  bind_text(stmt, 8, afl_version);
  bind_text(stmt, 9, ablation_mode);
  step_or_throw(db_, stmt, "insert_run");
}

void Database::finish_run(const std::string& id,
                          uint64_t end_ts,
                          const std::string& status,
                          const std::string& winner_status,
                          const RunLlmTotals& totals) {
  static constexpr const char* kSql =
      "UPDATE runs SET end_ts = ?, status = ?, winner_status = ?, "
      "llm_calls = ?, llm_failed_calls = ?, llm_input_tokens = ?, "
      "llm_output_tokens = ?, llm_total_latency_ms = ? WHERE id = ?";
  auto* stmt = prepared(&stmt_finish_run_, kSql);
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(end_ts));
  bind_text(stmt, 2, status);
  bind_text(stmt, 3, winner_status);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(totals.calls));
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(totals.failed_calls));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(totals.input_tokens));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(totals.output_tokens));
  sqlite3_bind_double(stmt, 8, totals.total_latency_ms);
  bind_text(stmt, 9, id);
  step_or_throw(db_, stmt, "finish_run");
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
  static constexpr const char* kSql =
      "INSERT OR REPLACE INTO campaigns (id, run_id, type, parent_campaign_id, intervention_id, "
      "output_dir, start_ts, end_ts, budget_sec, status, termination_reason) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, NULL, ?, ?, '')";
  auto* stmt = prepared(&stmt_insert_campaign_, kSql);
  bind_text(stmt, 1, id);
  bind_text(stmt, 2, run_id);
  bind_text(stmt, 3, type);
  bind_text(stmt, 4, parent_campaign_id);
  bind_text(stmt, 5, intervention_id);
  bind_text(stmt, 6, output_dir.string());
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(start_ts));
  sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(budget_sec));
  bind_text(stmt, 9, status);
  step_or_throw(db_, stmt, "insert_campaign");
}

void Database::finish_campaign(const std::string& id, uint64_t end_ts, const std::string& status) {
  // Back-compat: pass status as the termination_reason when the caller
  // didn't provide a structured reason. Avoids breaking older call sites.
  finish_campaign(id, end_ts, status, status);
}

void Database::finish_campaign(const std::string& id,
                               uint64_t end_ts,
                               const std::string& status,
                               const std::string& termination_reason) {
  static constexpr const char* kSql =
      "UPDATE campaigns SET end_ts = ?, status = ?, termination_reason = ? WHERE id = ?";
  auto* stmt = prepared(&stmt_finish_campaign_, kSql);
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(end_ts));
  bind_text(stmt, 2, status);
  bind_text(stmt, 3, termination_reason);
  bind_text(stmt, 4, id);
  step_or_throw(db_, stmt, "finish_campaign");
}

void Database::insert_telemetry(const std::string& campaign_id, const AflStats& stats) {
  static constexpr const char* kSql =
      "INSERT INTO telemetry (campaign_id, ts, execs_done, execs_per_sec, paths_total, "
      "edges_found, unique_crashes, unique_hangs, bitmap_cvg, recipe_hits, recipe_misses, stale, "
      "raw_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  auto* stmt = prepared(&stmt_insert_telemetry_, kSql);
  bind_text(stmt, 1, campaign_id);
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(stats.sampled_at));
  sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(stats.execs_done));
  sqlite3_bind_double(stmt, 4, stats.execs_per_sec);
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(stats.paths_total));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(stats.edges_found));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(stats.unique_crashes));
  sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(stats.unique_hangs));
  sqlite3_bind_double(stmt, 9, stats.bitmap_cvg);
  sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(stats.recipe_hits));
  sqlite3_bind_int64(stmt, 11, static_cast<sqlite3_int64>(stats.recipe_misses));
  sqlite3_bind_int(stmt, 12, stats.stale ? 1 : 0);
  const auto raw = afl_stats_json(stats);
  bind_text(stmt, 13, raw);
  step_or_throw(db_, stmt, "insert_telemetry");
}

void Database::insert_plateau(const PlateauEvent& event, const std::string& blackboard_json) {
  static constexpr const char* kSql =
      "INSERT INTO plateaus (id, run_id, campaign_id, detected_ts, window_sec, execs_delta, "
      "new_paths_delta, new_edges_delta, new_crashes_delta, sample_count, reason, blackboard_json) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  auto* stmt = prepared(&stmt_insert_plateau_, kSql);
  bind_text(stmt, 1, event.id);
  bind_text(stmt, 2, event.run_id);
  bind_text(stmt, 3, event.campaign_id);
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(event.detected_ts));
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(event.window_sec));
  sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(event.execs_delta));
  sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(event.new_paths_delta));
  sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(event.new_edges_delta));
  sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(event.new_crashes_delta));
  sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(event.sample_count));
  bind_text(stmt, 11, event.reason);
  bind_text(stmt, 12, blackboard_json);
  step_or_throw(db_, stmt, "insert_plateau");
}

void Database::insert_micro_result(const MicroResult& result) {
  static constexpr const char* kSql =
      "INSERT OR REPLACE INTO micro_results (id, intervention_id, strategy_ids_json, campaign_id, "
      "execs_done, new_paths, new_edges, unique_crashes, recipe_hits, recipe_misses, "
      "edge_score, path_score, crash_score, recipe_score, reward, edges_unavailable, promoted) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  auto* stmt = prepared(&stmt_insert_micro_result_, kSql);
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
  sqlite3_bind_double(stmt, 11, result.edge_score);
  sqlite3_bind_double(stmt, 12, result.path_score);
  sqlite3_bind_double(stmt, 13, result.crash_score);
  sqlite3_bind_double(stmt, 14, result.recipe_score);
  sqlite3_bind_double(stmt, 15, result.reward);
  sqlite3_bind_int(stmt, 16, result.edges_unavailable ? 1 : 0);
  sqlite3_bind_int(stmt, 17, result.promoted ? 1 : 0);
  step_or_throw(db_, stmt, "insert_micro_result");
}

void Database::insert_agent_decision(const AgentDecision& decision) {
  static constexpr const char* kSql =
      "INSERT OR REPLACE INTO agent_decisions (id, run_id, plateau_id, agent, model_provider, "
      "model_name, task_json, context_hash, response_hash, latency_ms, input_tokens, "
      "output_tokens, retry_count, error_kind, schema_valid, fallback_used, error, "
      "proposal_json, created_ts) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  auto* stmt = prepared(&stmt_insert_agent_decision_, kSql);
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
  sqlite3_bind_int64(stmt, 11, static_cast<sqlite3_int64>(decision.model_response.input_tokens));
  sqlite3_bind_int64(stmt, 12, static_cast<sqlite3_int64>(decision.model_response.output_tokens));
  sqlite3_bind_int(stmt, 13, static_cast<int>(decision.model_response.retry_count));
  bind_text(stmt, 14, decision.model_response.error_kind);
  sqlite3_bind_int(stmt, 15, decision.model_response.schema_valid ? 1 : 0);
  sqlite3_bind_int(stmt, 16, decision.fallback_used ? 1 : 0);
  bind_text(stmt, 17, decision.model_response.error);
  bind_text(stmt, 18, decision.proposal_json);
  sqlite3_bind_int64(stmt, 19, static_cast<sqlite3_int64>(decision.created_ts));
  step_or_throw(db_, stmt, "insert_agent_decision");
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
  static constexpr const char* kSql =
      "INSERT OR REPLACE INTO agent_memory (id, run_id, target_name, agent, memory_type, key, "
      "value_json, evidence_json, reward, confidence, updated_ts) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  auto* stmt = prepared(&stmt_insert_agent_memory_, kSql);
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
  step_or_throw(db_, stmt, "insert_agent_memory");
}

std::vector<std::string> Database::get_recent_decisions(const std::string& run_id, int limit) {
  std::vector<std::string> decisions;
  static constexpr const char* kSql =
      "SELECT proposal_json FROM agent_decisions WHERE run_id = ? "
      "ORDER BY created_ts DESC LIMIT ?";
  sqlite3_stmt* stmt = nullptr;
  try {
    stmt = prepared(&stmt_get_recent_decisions_, kSql);
  } catch (...) {
    return decisions;  // prepare failed; return empty
  }
  bind_text(stmt, 1, run_id);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    if (text) decisions.push_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return decisions;
}

std::vector<std::string> Database::get_agent_memory(const std::string& run_id) {
  std::vector<std::string> memory;
  static constexpr const char* kSql =
      "SELECT value_json FROM agent_memory WHERE run_id = ? ORDER BY updated_ts DESC";
  sqlite3_stmt* stmt = nullptr;
  try {
    stmt = prepared(&stmt_get_agent_memory_, kSql);
  } catch (...) {
    return memory;
  }
  bind_text(stmt, 1, run_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    if (text) memory.push_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return memory;
}

void Database::close() {
  // Order matters: finalize prepared statements before sqlite3_close,
  // otherwise close returns SQLITE_BUSY and we leak the connection.
  if (txn_depth_ > 0) {
    // Don't leave the WAL in a half-open state if we're tearing down
    // mid-transaction. Rollback is best-effort; failures are logged
    // and ignored.
    rollback();
  }
  finalize_all();
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

}  // namespace fuzzpilot
