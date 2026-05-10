#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace fuzzpilot {

struct AflStats {
  std::map<std::string, std::string> raw;
  std::time_t sampled_at = 0;
  uint64_t execs_done = 0;
  double execs_per_sec = 0.0;
  uint64_t paths_total = 0;
  uint64_t paths_favored = 0;
  uint64_t paths_found = 0;
  uint64_t paths_imported = 0;
  uint64_t max_depth = 0;
  uint64_t cur_path = 0;
  uint64_t pending_favs = 0;
  uint64_t pending_total = 0;
  uint64_t variable_paths = 0;
  uint64_t unique_crashes = 0;
  uint64_t unique_hangs = 0;
  double bitmap_cvg = 0.0;
  double stability = 0.0;
  uint64_t last_path = 0;
  uint64_t last_crash = 0;
  uint64_t last_hang = 0;
  uint64_t recipe_hits = 0;
  uint64_t recipe_misses = 0;
};

std::optional<AflStats> parse_fuzzer_stats(const std::filesystem::path& path,
                                           std::string* error);
std::string afl_stats_json(const AflStats& stats);
std::string afl_stats_summary(const AflStats& stats);

}  // namespace fuzzpilot

