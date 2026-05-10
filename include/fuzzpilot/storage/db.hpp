#pragma once

#include "fuzzpilot/plateau/detector.hpp"
#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <filesystem>
#include <string>

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
  void insert_telemetry(const std::string& campaign_id, const AflStats& stats);
  void insert_plateau(const PlateauEvent& event, const std::string& blackboard_json);
  void insert_micro_result(const MicroResult& result);
  void insert_agent_decision(const AgentDecision& decision);
  void close();

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace fuzzpilot
