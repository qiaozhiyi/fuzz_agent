#pragma once

#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

namespace fuzzpilot {

struct AgentDecision;
struct MicroResult;

class Database {
 public:
  Database();
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  void open(const std::filesystem::path& path);
  void initialize_schema(const std::filesystem::path& schema_path);
  void execute(const std::string& sql);
  void insert_run(const std::string& id,
                  const std::string& project,
                  const std::string& target_name,
                  uint64_t start_ts,
                  const std::string& status,
                  const std::string& os,
                  const std::string& arch,
                  const std::string& afl_version);
  void finish_run(const std::string& id, uint64_t end_ts, const std::string& status);
  void insert_campaign(const std::string& id,
                       const std::string& run_id,
                       const std::string& type,
                       const std::string& parent_campaign_id,
                       const std::string& intervention_id,
                       const std::filesystem::path& output_dir,
                       uint64_t start_ts,
                       uint64_t budget_sec,
                       const std::string& status);
  void finish_campaign(const std::string& id, uint64_t end_ts, const std::string& status);
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
  sqlite3* db_ = nullptr;
};

}  // namespace fuzzpilot
