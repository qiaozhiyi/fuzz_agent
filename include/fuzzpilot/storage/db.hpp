#pragma once

#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace fuzzpilot {

struct AgentDecision;
struct MicroResult;

// Aggregate model usage accumulated by the controller and persisted to
// runs.finish_run when the run terminates. Kept in a struct so the
// caller can pass it as one argument rather than 5 scalars.
struct RunLlmTotals {
  uint64_t calls = 0;
  uint64_t failed_calls = 0;
  uint64_t input_tokens = 0;
  uint64_t output_tokens = 0;
  double total_latency_ms = 0.0;
};

class Database {
 public:
  Database();
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  void open(const std::filesystem::path& path);
  void initialize_schema(const std::filesystem::path& schema_path);
  void execute(const std::string& sql);

  // Explicit transaction API. Callers can wrap a batch of inserts in
  // begin()/commit() to amortize WAL fsync cost. If a transaction is
  // already open, begin() is a no-op (nested counter). Mismatched
  // begin/commit calls log to stderr but never throw — the controller
  // must remain robust when an exception unwinds mid-batch.
  void begin();
  void commit();
  void rollback();

  void insert_run(const std::string& id,
                  const std::string& project,
                  const std::string& target_name,
                  uint64_t start_ts,
                  const std::string& status,
                  const std::string& os,
                  const std::string& arch,
                  const std::string& afl_version,
                  const std::string& ablation_mode);
  // Persist final run state. winner_status and totals can be empty/zero
  // if the run did not reach the corresponding stage.
  void finish_run(const std::string& id,
                  uint64_t end_ts,
                  const std::string& status,
                  const std::string& winner_status,
                  const RunLlmTotals& totals);
  void insert_campaign(const std::string& id,
                       const std::string& run_id,
                       const std::string& type,
                       const std::string& parent_campaign_id,
                       const std::string& intervention_id,
                       const std::filesystem::path& output_dir,
                       uint64_t start_ts,
                       uint64_t budget_sec,
                       const std::string& status);
  // The two-argument overload preserves backward compatibility (sets
  // termination_reason to the status text); the new overload lets the
  // controller record the structured reason separately.
  void finish_campaign(const std::string& id, uint64_t end_ts, const std::string& status);
  void finish_campaign(const std::string& id,
                       uint64_t end_ts,
                       const std::string& status,
                       const std::string& termination_reason);
  void insert_telemetry(const std::string& campaign_id, const AflStats& stats);
  void insert_plateau(const PlateauEvent& event, const std::string& blackboard_json);
  void insert_micro_result(const MicroResult& result);
  void insert_agent_decision(const AgentDecision& decision);
  void insert_agent_memory(const std::string& id,
                           const std::string& run_id,
                           const std::string& target_name,
                           const std::string& agent,
                           const std::string& memory_type,
                           const std::string& key,
                           const std::string& value_json,
                           const std::string& evidence_json,
                           double reward,
                           double confidence,
                           uint64_t updated_ts);

  std::vector<std::string> get_recent_decisions(const std::string& run_id, int limit);
  std::vector<std::string> get_agent_memory(const std::string& run_id);

  void close();

 private:
  // Lazily prepare a cached statement. Returns nullptr if prepare fails
  // and writes the SQLite error message to out_err; the caller decides
  // whether to throw. Prepared statements are reset between uses so the
  // hot path avoids the parse+plan cost of sqlite3_prepare_v2.
  sqlite3_stmt* prepared(sqlite3_stmt** slot, const char* sql);
  void finalize_all();

  // Apply pragmas that aren't safe to put in schema.sql (since schema
  // is only loaded on initialize, not on subsequent opens). Idempotent.
  void apply_runtime_pragmas();

  sqlite3* db_ = nullptr;
  // Cached prepared statements. nullptr until first use; finalized in
  // close()/dtor.
  sqlite3_stmt* stmt_insert_run_ = nullptr;
  sqlite3_stmt* stmt_finish_run_ = nullptr;
  sqlite3_stmt* stmt_insert_campaign_ = nullptr;
  sqlite3_stmt* stmt_finish_campaign_ = nullptr;
  sqlite3_stmt* stmt_insert_telemetry_ = nullptr;
  sqlite3_stmt* stmt_insert_plateau_ = nullptr;
  sqlite3_stmt* stmt_insert_micro_result_ = nullptr;
  sqlite3_stmt* stmt_insert_agent_decision_ = nullptr;
  sqlite3_stmt* stmt_insert_agent_memory_ = nullptr;
  sqlite3_stmt* stmt_get_recent_decisions_ = nullptr;
  sqlite3_stmt* stmt_get_agent_memory_ = nullptr;
  // Transaction nesting depth — we only emit BEGIN/COMMIT when depth
  // transitions across zero, so callers may freely begin() inside a
  // larger transaction without surprises.
  int txn_depth_ = 0;
};

}  // namespace fuzzpilot
