#include "fuzzpilot/telemetry/collector.hpp"

#include "fuzzpilot/telemetry/mutation_events.hpp"

#include <ctime>

namespace fuzzpilot {
namespace {
// Samples whose AFL last_update is more than this many seconds older
// than wall-clock are flagged stale. AFL++ writes fuzzer_stats every
// few seconds; 90s is a generous slack that still catches a frozen
// fuzzer.
constexpr std::time_t kDefaultMaxStalenessSec = 90;

void merge_into_stats(AflStats& stats, const MutationTelemetrySnapshot& snap) {
  stats.recipe_hits = snap.recipe_hits;
  stats.recipe_misses = snap.recipe_misses;
  stats.raw["recipe_hits"] = std::to_string(snap.recipe_hits);
  stats.raw["recipe_misses"] = std::to_string(snap.recipe_misses);
  stats.raw["mutation_count"] = std::to_string(snap.mutation_count);
}
}  // namespace

TelemetryCollector::TelemetryCollector(std::filesystem::path campaign_dir)
    : campaign_dir_(std::move(campaign_dir)) {}

TelemetryCollector::TelemetryCollector(std::filesystem::path campaign_dir,
                                       std::filesystem::path mutator_telemetry_path)
    : campaign_dir_(std::move(campaign_dir)),
      mutator_telemetry_path_(std::move(mutator_telemetry_path)) {}

void TelemetryCollector::reset_incremental_state() {
  mutator_telemetry_offset_ = 0;
  mutator_hits_total_ = 0;
  mutator_misses_total_ = 0;
  mutator_count_total_ = 0;
  mutator_operator_counts_.clear();
  fuzzer_stats_mtime_ = {};
  fuzzer_stats_cache_.reset();
  mutator_telemetry_mtime_ = {};
}

std::optional<AflStats> TelemetryCollector::sample(std::string* error) {
  const auto stats_path = find_fuzzer_stats_file(campaign_dir_);
  // mtime-based cache. If AFL hasn't written since last sample we
  // reuse the parsed result; otherwise re-parse. This skips most of
  // the per-tick I/O on long-running campaigns where the file is
  // rewritten only every few seconds.
  std::error_code ec;
  auto current_mtime = std::filesystem::last_write_time(stats_path, ec);
  AflStats stats;
  bool used_cache = false;
  if (!ec && fuzzer_stats_cache_ && current_mtime == fuzzer_stats_mtime_) {
    stats = *fuzzer_stats_cache_;
    used_cache = true;
  } else {
    auto parsed = parse_fuzzer_stats(stats_path, error);
    if (!parsed) {
      return parsed;
    }
    stats = std::move(*parsed);
    if (!ec) {
      fuzzer_stats_mtime_ = current_mtime;
      fuzzer_stats_cache_ = stats;
    }
  }

  // Staleness — compare AFL's `last_update` against wall-clock. The
  // check fires regardless of cache hit/miss because wall-clock keeps
  // moving even when the file does not.
  const auto now = std::time(nullptr);
  if (stats.sampled_at > 0 &&
      now > stats.sampled_at &&
      (now - stats.sampled_at) > kDefaultMaxStalenessSec) {
    stats.stale = true;
  }
  (void)used_cache;  // currently used only for debugging; reserved

  // Mutator telemetry: increment-only read. Resolved once per call
  // (AFL may create the file later than fuzzer_stats).
  auto mutator_path = mutator_telemetry_path_;
  if (mutator_path.empty()) {
    mutator_path = find_mutator_telemetry_file(campaign_dir_);
  }
  if (!mutator_path.empty()) {
    // mtime check — if the file's mtime moved backward (rotation or
    // overwrite), the size-based detection in
    // parse_mutator_telemetry_incremental misses it. Reset accumulator
    // state before re-parsing so we don't double-count.
    std::error_code mt_ec;
    auto mutator_mtime = std::filesystem::last_write_time(mutator_path, mt_ec);
    if (!mt_ec) {
      // First sample for this collector: just remember the mtime.
      // Subsequent samples: detect rewind.
      if (mutator_telemetry_mtime_.time_since_epoch().count() != 0 &&
          mutator_mtime < mutator_telemetry_mtime_) {
        mutator_telemetry_offset_ = 0;
        mutator_hits_total_ = 0;
        mutator_misses_total_ = 0;
        mutator_count_total_ = 0;
        mutator_operator_counts_.clear();
      }
      mutator_telemetry_mtime_ = mutator_mtime;
    }
    MutationTelemetrySnapshot snap;
    snap.recipe_hits = mutator_hits_total_;
    snap.recipe_misses = mutator_misses_total_;
    snap.mutation_count = mutator_count_total_;
    snap.operator_counts = mutator_operator_counts_;
    std::string mutator_error;
    if (parse_mutator_telemetry_incremental(mutator_path,
                                            mutator_telemetry_offset_,
                                            snap, &mutator_error)) {
      mutator_hits_total_ = snap.recipe_hits;
      mutator_misses_total_ = snap.recipe_misses;
      mutator_count_total_ = snap.mutation_count;
      mutator_operator_counts_ = snap.operator_counts;
      merge_into_stats(stats, snap);
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
