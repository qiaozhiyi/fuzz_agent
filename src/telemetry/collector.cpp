#include "fuzzpilot/telemetry/collector.hpp"

#include "fuzzpilot/telemetry/mutation_events.hpp"

namespace fuzzpilot {

TelemetryCollector::TelemetryCollector(std::filesystem::path campaign_dir)
    : campaign_dir_(std::move(campaign_dir)) {}

TelemetryCollector::TelemetryCollector(std::filesystem::path campaign_dir,
                                       std::filesystem::path mutator_telemetry_path)
    : campaign_dir_(std::move(campaign_dir)),
      mutator_telemetry_path_(std::move(mutator_telemetry_path)) {}

std::optional<AflStats> TelemetryCollector::sample(std::string* error) const {
  auto stats = parse_fuzzer_stats(find_fuzzer_stats_file(campaign_dir_), error);
  if (!stats) {
    return stats;
  }
  auto mutator_path = mutator_telemetry_path_;
  if (mutator_path.empty()) {
    mutator_path = find_mutator_telemetry_file(campaign_dir_);
  }
  if (!mutator_path.empty() && std::filesystem::exists(mutator_path)) {
    std::string mutator_error;
    const auto mutation = parse_mutator_telemetry(mutator_path, &mutator_error);
    if (mutation) {
      stats->recipe_hits = mutation->recipe_hits;
      stats->recipe_misses = mutation->recipe_misses;
      stats->raw["recipe_hits"] = std::to_string(mutation->recipe_hits);
      stats->raw["recipe_misses"] = std::to_string(mutation->recipe_misses);
      stats->raw["mutation_count"] = std::to_string(mutation->mutation_count);
    }
  }
  return stats;
}

std::filesystem::path find_fuzzer_stats_file(const std::filesystem::path& campaign_dir) {
  const auto direct = campaign_dir / "fuzzer_stats";
  if (std::filesystem::exists(direct)) {
    return direct;
  }
  const auto default_worker = campaign_dir / "default" / "fuzzer_stats";
  if (std::filesystem::exists(default_worker)) {
    return default_worker;
  }
  return direct;
}

std::filesystem::path find_mutator_telemetry_file(const std::filesystem::path& campaign_dir) {
  const auto direct = campaign_dir / "mutator_telemetry.jsonl";
  if (std::filesystem::exists(direct)) {
    return direct;
  }
  const auto default_worker = campaign_dir / "default" / "mutator_telemetry.jsonl";
  if (std::filesystem::exists(default_worker)) {
    return default_worker;
  }
  return {};
}

}  // namespace fuzzpilot
