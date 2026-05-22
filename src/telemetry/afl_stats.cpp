#include "fuzzpilot/telemetry/afl_stats.hpp"
#include "fuzzpilot/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fuzzpilot {
namespace {



uint64_t as_u64(const std::map<std::string, std::string>& raw, const std::string& key) {
  const auto it = raw.find(key);
  if (it == raw.end()) {
    return 0;
  }
  try {
    return static_cast<uint64_t>(std::stoull(it->second));
  } catch (...) {
    return 0;
  }
}

double as_double(const std::map<std::string, std::string>& raw, const std::string& key) {
  const auto it = raw.find(key);
  if (it == raw.end()) {
    return 0.0;
  }
  std::string value = it->second;
  value.erase(std::remove(value.begin(), value.end(), '%'), value.end());
  try {
    return std::stod(value);
  } catch (...) {
    return 0.0;
  }
}



uint64_t first_nonzero(const AflStats& stats, const std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const auto value = as_u64(stats.raw, key);
    if (value != 0) {
      return value;
    }
  }
  return 0;
}

}  // namespace

std::optional<AflStats> parse_fuzzer_stats(const std::filesystem::path& path,
                                           std::string* error) {
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open fuzzer_stats: " + path.string();
    }
    return std::nullopt;
  }

  AflStats stats;
  stats.sampled_at = std::time(nullptr);
  std::string line;
  while (std::getline(input, line)) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    // Performance optimization: Cast to std::string_view before substr
    // to avoid temporary heap allocations during the parsing loop.
    const auto key = trim(std::string_view(line).substr(0, colon));
    const auto value = trim(std::string_view(line).substr(colon + 1));
    if (!key.empty()) {
      stats.raw[std::string(key)] = std::string(value);
    }
  }

  if (const auto last_update = as_u64(stats.raw, "last_update"); last_update != 0) {
    stats.sampled_at = static_cast<std::time_t>(last_update);
  }
  stats.execs_done = as_u64(stats.raw, "execs_done");
  stats.execs_per_sec = as_double(stats.raw, "execs_per_sec");
  stats.paths_total = as_u64(stats.raw, "paths_total");
  stats.paths_favored = as_u64(stats.raw, "paths_favored");
  stats.paths_found = as_u64(stats.raw, "paths_found");
  stats.paths_imported = as_u64(stats.raw, "paths_imported");
  // AFL++ 3.x calls this `edges_found`; some forks emit `edge_count` / `total_edges`.
  // Prefer absolute edge count over bitmap percentage for statistical comparison.
  stats.edges_found =
      first_nonzero(stats, {"edges_found", "edge_count", "total_edges", "fuzzed_edges"});
  stats.max_depth = as_u64(stats.raw, "max_depth");
  stats.cur_path = as_u64(stats.raw, "cur_path");
  stats.pending_favs = as_u64(stats.raw, "pending_favs");
  stats.pending_total = as_u64(stats.raw, "pending_total");
  stats.variable_paths = as_u64(stats.raw, "variable_paths");
  stats.unique_crashes = as_u64(stats.raw, "unique_crashes");
  stats.unique_hangs = as_u64(stats.raw, "unique_hangs");
  stats.bitmap_cvg = as_double(stats.raw, "bitmap_cvg");
  stats.stability = as_double(stats.raw, "stability");
  stats.last_path = first_nonzero(stats, {"last_path", "last_find", "last_path_time"});
  stats.last_crash = as_u64(stats.raw, "last_crash");
  stats.last_hang = as_u64(stats.raw, "last_hang");
  stats.recipe_hits = as_u64(stats.raw, "recipe_hits");
  stats.recipe_misses = as_u64(stats.raw, "recipe_misses");
  // Trim already-promoted keys from raw so the on-disk JSON (and the
  // in-memory map carried in every telemetry sample) stays bounded.
  // Without this, a 24h run accumulates several hundred MB of
  // redundant strings since each sample copies the entire raw map.
  static const std::initializer_list<const char*> kTypedKeys = {
      "last_update", "execs_done", "execs_per_sec", "paths_total",
      "paths_favored", "paths_found", "paths_imported",
      "edges_found", "edge_count", "total_edges", "fuzzed_edges",
      "max_depth", "cur_path", "pending_favs", "pending_total",
      "variable_paths", "unique_crashes", "unique_hangs",
      "bitmap_cvg", "stability",
      "last_path", "last_find", "last_path_time",
      "last_crash", "last_hang",
      "recipe_hits", "recipe_misses",
  };
  for (const char* key : kTypedKeys) {
    stats.raw.erase(key);
  }
  return stats;
}

std::string afl_stats_json(const AflStats& stats) {
  // Performance optimization: Avoid std::ostringstream allocations
  std::string out;
  out.reserve(512); // Pre-allocate enough capacity for typical JSON size
  out += "{";
  out += "\"ts\":"; out += std::to_string(stats.sampled_at); out += ",";
  out += "\"execs_done\":"; out += std::to_string(stats.execs_done); out += ",";
  out += "\"execs_per_sec\":"; out += std::to_string(stats.execs_per_sec); out += ",";
  out += "\"paths_total\":"; out += std::to_string(stats.paths_total); out += ",";
  out += "\"edges_found\":"; out += std::to_string(stats.edges_found); out += ",";
  out += "\"stale\":"; out += (stats.stale ? "true" : "false"); out += ",";
  out += "\"unique_crashes\":"; out += std::to_string(stats.unique_crashes); out += ",";
  out += "\"unique_hangs\":"; out += std::to_string(stats.unique_hangs); out += ",";
  out += "\"bitmap_cvg\":"; out += std::to_string(stats.bitmap_cvg); out += ",";
  out += "\"stability\":"; out += std::to_string(stats.stability); out += ",";
  out += "\"last_path\":"; out += std::to_string(stats.last_path); out += ",";
  out += "\"recipe_hits\":"; out += std::to_string(stats.recipe_hits); out += ",";
  out += "\"recipe_misses\":"; out += std::to_string(stats.recipe_misses); out += ",";
  out += "\"raw\":{";
  bool first = true;
  for (const auto& [key, value] : stats.raw) {
    if (!first) {
      out += ",";
    }
    first = false;
    out += "\""; out += json_escape(key); out += "\":\""; out += json_escape(value); out += "\"";
  }
  out += "}}";
  return out;
}

std::string afl_stats_summary(const AflStats& stats) {
  // Performance optimization: Avoid std::ostringstream allocations
  std::string out;
  out += "execs_done=";
  out += std::to_string(stats.execs_done);
  out += " execs_per_sec=";
  out += std::to_string(stats.execs_per_sec);
  out += " paths_total=";
  out += std::to_string(stats.paths_total);
  out += " edges_found=";
  out += std::to_string(stats.edges_found);
  out += " unique_crashes=";
  out += std::to_string(stats.unique_crashes);
  out += " unique_hangs=";
  out += std::to_string(stats.unique_hangs);
  out += " bitmap_cvg=";
  out += std::to_string(stats.bitmap_cvg);
  out += " stability=";
  out += std::to_string(stats.stability);
  return out;
}

}  // namespace fuzzpilot
