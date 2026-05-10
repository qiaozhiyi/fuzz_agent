#pragma once

#include "fuzzpilot/config.hpp"
#include "fuzzpilot/interventions/intervention.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fuzzpilot {

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
  uint32_t budget_sec = 0;
  bool dry_run = true;
};

CorpusSnapshotResult snapshot_corpus(const std::filesystem::path& afl_output_dir,
                                     const std::filesystem::path& snapshot_dir);

std::vector<MicroCampaignSpec> plan_micro_campaigns(const AppConfig& config,
                                                    const std::string& plateau_id,
                                                    const std::filesystem::path& snapshot_dir,
                                                    const std::filesystem::path& work_dir,
                                                    bool dry_run);

void prepare_micro_campaigns(const std::vector<MicroCampaignSpec>& specs);
std::string corpus_snapshot_json(const CorpusSnapshotResult& snapshot);
std::string micro_campaign_spec_json(const MicroCampaignSpec& spec);

}  // namespace fuzzpilot

