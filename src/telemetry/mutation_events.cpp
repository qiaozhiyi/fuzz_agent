#include "fuzzpilot/telemetry/mutation_events.hpp"
#include "fuzzpilot/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fuzzpilot {
namespace {



uint64_t find_u64(const std::string& line, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) {
    return 0;
  }
  auto start = pos + needle.size();
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  auto end = start;
  while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) {
    ++end;
  }
  if (end == start) {
    return 0;
  }
  try {
    return static_cast<uint64_t>(std::stoull(line.substr(start, end - start)));
  } catch (...) {
    return 0;
  }
}

void parse_operator_counts(const std::string& line,
                           std::map<std::string, uint64_t>& operator_counts) {
  const std::string needle = "\"operators\":{";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) {
    return;
  }
  const auto start = pos + needle.size();
  const auto end = line.find('}', start);
  if (end == std::string::npos || end <= start) {
    return;
  }
  std::string body = line.substr(start, end - start);
  std::size_t cursor = 0;
  while (cursor < body.size()) {
    const auto quote1 = body.find('"', cursor);
    if (quote1 == std::string::npos) {
      break;
    }
    const auto quote2 = body.find('"', quote1 + 1);
    if (quote2 == std::string::npos) {
      break;
    }
    const auto colon = body.find(':', quote2 + 1);
    if (colon == std::string::npos) {
      break;
    }
    auto value_end = colon + 1;
    while (value_end < body.size() &&
           std::isdigit(static_cast<unsigned char>(body[value_end]))) {
      ++value_end;
    }
    const auto op = body.substr(quote1 + 1, quote2 - quote1 - 1);
    try {
      operator_counts[op] += static_cast<uint64_t>(
          std::stoull(body.substr(colon + 1, value_end - colon - 1)));
    } catch (const std::exception&) {
      // Numeric parse failed (overflow / malformed). Don't change
      // the running counter — the previous total stays in place and
      // we move on to the next operator. Sentinel value -1 in the
      // record would be visible via the count comparison in the
      // analysis layer; here we just refuse to corrupt the total.
    }
    cursor = value_end + 1;
  }
}



}  // namespace

std::optional<MutationTelemetrySnapshot> parse_mutator_telemetry(
    const std::filesystem::path& path,
    std::string* error) {
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open mutator telemetry: " + path.string();
    }
    return std::nullopt;
  }

  MutationTelemetrySnapshot snapshot;
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    snapshot.recipe_hits += find_u64(line, "recipe_hits");
    snapshot.recipe_misses += find_u64(line, "recipe_misses");
    snapshot.mutation_count += find_u64(line, "mutation_count");
    parse_operator_counts(line, snapshot.operator_counts);
  }
  return snapshot;
}

bool parse_mutator_telemetry_incremental(
    const std::filesystem::path& path,
    std::uint64_t& start_offset,
    MutationTelemetrySnapshot& accumulator,
    std::string* error) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    // Not yet created by AFL — treat as success with no delta.
    return true;
  }
  const auto file_size = static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
  if (ec) {
    if (error) *error = "stat failed: " + ec.message();
    return false;
  }
  if (file_size < start_offset) {
    // File shrank or was rotated by AFL. Reset accumulator and reparse
    // from scratch so we don't double-count nor lose data.
    accumulator = MutationTelemetrySnapshot{};
    start_offset = 0;
  }
  if (file_size == start_offset) {
    // No new content since last read. Cheap path — nothing to do.
    return true;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error) *error = "failed to open mutator telemetry: " + path.string();
    return false;
  }
  input.seekg(static_cast<std::streamoff>(start_offset));
  if (!input) {
    if (error) *error = "seek failed in mutator telemetry: " + path.string();
    return false;
  }

  // Read remaining bytes. We deliberately read line-by-line via
  // getline so that a partially-flushed trailing line is left for the
  // next iteration — we only advance start_offset to the end of the
  // last fully-terminated line.
  std::uint64_t consumed = start_offset;
  std::string line;
  while (true) {
    if (!std::getline(input, line)) {
      break;
    }
    const auto after = static_cast<std::uint64_t>(input.tellg());
    // getline returns true even for an unterminated last line; detect
    // that by checking eof and bail without advancing the offset, so
    // the partial line is reparsed next time once AFL finishes writing.
    if (input.eof()) {
      input.clear();
      if (after == file_size) {
        // Line was complete (file ends with newline). Commit it.
        consumed = after;
      }
      break;
    }
    consumed = after;
    std::string trimmed = trim(line);
    if (trimmed.empty()) {
      continue;
    }
    accumulator.recipe_hits += find_u64(trimmed, "recipe_hits");
    accumulator.recipe_misses += find_u64(trimmed, "recipe_misses");
    accumulator.mutation_count += find_u64(trimmed, "mutation_count");
    parse_operator_counts(trimmed, accumulator.operator_counts);
  }
  start_offset = consumed;
  return true;
}

std::string mutation_telemetry_json(const MutationTelemetrySnapshot& snapshot) {
  std::ostringstream out;
  out << "{";
  out << "\"recipe_hits\":" << snapshot.recipe_hits << ",";
  out << "\"recipe_misses\":" << snapshot.recipe_misses << ",";
  out << "\"mutation_count\":" << snapshot.mutation_count << ",";
  out << "\"operators\":{";
  bool first = true;
  for (const auto& [op, count] : snapshot.operator_counts) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << "\"" << json_escape(op) << "\":" << count;
  }
  out << "}}";
  return out.str();
}

}  // namespace fuzzpilot

