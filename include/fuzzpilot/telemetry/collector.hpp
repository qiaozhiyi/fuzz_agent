#pragma once

#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace fuzzpilot {

class TelemetryCollector {
 public:
  explicit TelemetryCollector(std::filesystem::path campaign_dir);
  TelemetryCollector(std::filesystem::path campaign_dir,
                     std::filesystem::path mutator_telemetry_path);

  std::optional<AflStats> sample(std::string* error) const;
  const std::filesystem::path& campaign_dir() const { return campaign_dir_; }

 private:
  std::filesystem::path campaign_dir_;
  std::filesystem::path mutator_telemetry_path_;
};

std::filesystem::path find_fuzzer_stats_file(const std::filesystem::path& campaign_dir);
std::filesystem::path find_mutator_telemetry_file(const std::filesystem::path& campaign_dir);

}  // namespace fuzzpilot
