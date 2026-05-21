#pragma once

#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace fuzzpilot {

class TelemetryCollector {
 public:
  explicit TelemetryCollector(std::filesystem::path campaign_dir);
  TelemetryCollector(std::filesystem::path campaign_dir,
                     std::filesystem::path mutator_telemetry_path);

  // Sample once. NOT const — the collector maintains incremental state
  // (last-parsed mutator-telemetry offset, fuzzer_stats mtime cache,
  // running operator counts) so successive calls do not re-read the
  // whole file from disk.
  std::optional<AflStats> sample(std::string* error);
  const std::filesystem::path& campaign_dir() const { return campaign_dir_; }

  // Discard cached parser state (used by the test harness; not needed
  // by production code, which constructs a fresh collector per
  // campaign).
  void reset_incremental_state();

 private:
  std::filesystem::path campaign_dir_;
  std::filesystem::path mutator_telemetry_path_;
  // Byte offset into mutator_telemetry.jsonl that we have already
  // parsed. Subsequent reads seek here and only consume the tail.
  std::uint64_t mutator_telemetry_offset_ = 0;
  // Cumulative counters maintained across incremental reads. Reset
  // when the file shrinks (i.e. AFL restarted and rotated the file).
  std::uint64_t mutator_hits_total_ = 0;
  std::uint64_t mutator_misses_total_ = 0;
  std::uint64_t mutator_count_total_ = 0;
  std::map<std::string, std::uint64_t> mutator_operator_counts_;
  // fuzzer_stats mtime cache. We only re-parse when the file mtime
  // changes — saves several disk I/O + a few KB of parsing each tick
  // on long-running campaigns.
  std::filesystem::file_time_type fuzzer_stats_mtime_{};
  std::optional<AflStats> fuzzer_stats_cache_;
  // mtime of mutator_telemetry.jsonl at the time we last advanced
  // mutator_telemetry_offset_. If the file's mtime moves BACK (rotation
  // or restore), the offset is no longer valid and we reset state.
  // Catches the case where the file size happens to match but the
  // content was replaced — the size-only check in
  // parse_mutator_telemetry_incremental would miss it.
  std::filesystem::file_time_type mutator_telemetry_mtime_{};
};

std::filesystem::path find_fuzzer_stats_file(const std::filesystem::path& campaign_dir);
std::filesystem::path find_mutator_telemetry_file(const std::filesystem::path& campaign_dir);

}  // namespace fuzzpilot
