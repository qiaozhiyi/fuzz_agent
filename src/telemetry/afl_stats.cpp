#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fuzzpilot {
namespace {

std::string trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

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

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      default: out << c; break;
    }
  }
  return out.str();
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
    const auto key = trim(line.substr(0, colon));
    const auto value = trim(line.substr(colon + 1));
    if (!key.empty()) {
      stats.raw[key] = value;
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
  std::ostringstream out;
  out << "{";
  out << "\"ts\":" << stats.sampled_at << ",";
  out << "\"execs_done\":" << stats.execs_done << ",";
  out << "\"execs_per_sec\":" << stats.execs_per_sec << ",";
  out << "\"paths_total\":" << stats.paths_total << ",";
  out << "\"edges_found\":" << stats.edges_found << ",";
  out << "\"stale\":" << (stats.stale ? "true" : "false") << ",";
  out << "\"unique_crashes\":" << stats.unique_crashes << ",";
  out << "\"unique_hangs\":" << stats.unique_hangs << ",";
  out << "\"bitmap_cvg\":" << stats.bitmap_cvg << ",";
  out << "\"stability\":" << stats.stability << ",";
  out << "\"last_path\":" << stats.last_path << ",";
  out << "\"recipe_hits\":" << stats.recipe_hits << ",";
  out << "\"recipe_misses\":" << stats.recipe_misses << ",";
  out << "\"raw\":{";
  bool first = true;
  for (const auto& [key, value] : stats.raw) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
  }
  out << "}}";
  return out.str();
}

std::string afl_stats_summary(const AflStats& stats) {
  std::ostringstream out;
  out << "execs_done=" << stats.execs_done
      << " execs_per_sec=" << stats.execs_per_sec
      << " paths_total=" << stats.paths_total
      << " edges_found=" << stats.edges_found
      << " unique_crashes=" << stats.unique_crashes
      << " unique_hangs=" << stats.unique_hangs
      << " bitmap_cvg=" << stats.bitmap_cvg
      << " stability=" << stats.stability;
  return out.str();
}

}  // namespace fuzzpilot
