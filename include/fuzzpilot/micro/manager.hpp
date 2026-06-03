#pragma once

#include "fuzzpilot/config.hpp"
#include "fuzzpilot/interventions/intervention.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fuzzpilot {

class Database;

struct CorpusSnapshotResult {
  std::filesystem::path source_queue;
  std::filesystem::path snapshot_dir;
  uint64_t files_copied = 0;
  uint64_t bytes_copied = 0;
};

struct MicroCampaignSpec {
  std::string id;
  std::string intervention_id;
  std::string name;
  std::filesystem::path input_dir;
  std::filesystem::path output_dir;
  std::filesystem::path recipe_store;
  std::vector<std::string> tokens;
  std::string agent;
  std::string source_decision_id;
  std::string params_json;
  int spiral_stage = 0;
  double priority = 0.0;
  std::string depends_on_intervention_id;
  uint32_t budget_sec = 0;
  bool dry_run = true;
};

struct MicroBanditCandidate {
  std::string campaign_id;
  double mean_reward = 0.0;
  uint32_t budget_sec = 0;
};

struct MicroBanditRank {
  std::string campaign_id;
  double score = 0.0;
};

CorpusSnapshotResult snapshot_corpus(const std::filesystem::path& afl_output_dir,
                                     const std::filesystem::path& snapshot_dir);

std::vector<MicroCampaignSpec> plan_micro_campaigns(const AppConfig& config,
                                                    const std::string& plateau_id,
                                                    const std::filesystem::path& snapshot_dir,
                                                    const std::filesystem::path& work_dir,
                                                    bool dry_run,
                                                    Database* db = nullptr,
                                                    const std::string& run_id = "");

void prepare_micro_campaigns(const std::vector<MicroCampaignSpec>& specs,
                             const std::vector<std::string>& llm_tokens = {});
std::vector<MicroBanditRank> rank_micro_bandit_candidates(
    const std::vector<MicroBanditCandidate>& candidates,
    double total_budget_sec,
    double exploration_c);
std::string corpus_snapshot_json(const CorpusSnapshotResult& snapshot);
std::string micro_campaign_spec_json(const MicroCampaignSpec& spec);

}  // namespace fuzzpilot
