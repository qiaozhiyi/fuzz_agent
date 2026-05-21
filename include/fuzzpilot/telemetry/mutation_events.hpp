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

// Incremental variant: read only the JSONL lines starting from byte
// offset `start_offset`. On success, updates `start_offset` in place to
// point at the next unread byte (callers use this for subsequent
// calls). The delta is accumulated into `accumulator` rather than
// returned standalone — this lets the caller maintain running totals
// without re-walking the file. Returns true on success (including the
// "no new lines" case); false on read error and sets `error`.
//
// If the file size is smaller than `start_offset` (file was rotated),
// the offset is reset to 0 and the entire file is parsed fresh.
bool parse_mutator_telemetry_incremental(
    const std::filesystem::path& path,
    std::uint64_t& start_offset,
    MutationTelemetrySnapshot& accumulator,
    std::string* error);

std::string mutation_telemetry_json(const MutationTelemetrySnapshot& snapshot);

}  // namespace fuzzpilot

