#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace fuzzpilot {

struct MutationTelemetrySnapshot {
  uint64_t recipe_hits = 0;
  uint64_t recipe_misses = 0;
  uint64_t mutation_count = 0;
  std::map<std::string, uint64_t> operator_counts;
};

std::optional<MutationTelemetrySnapshot> parse_mutator_telemetry(
    const std::filesystem::path& path,
    std::string* error);

std::string mutation_telemetry_json(const MutationTelemetrySnapshot& snapshot);

}  // namespace fuzzpilot

