#include "fuzzpilot/mutation/range_selector.hpp"

#include <algorithm>
#include <limits>

namespace fuzzpilot {

bool offset_in_ranges(std::size_t offset, const std::vector<ByteRange>& ranges) {
  for (const auto& range : ranges) {
    if (offset >= range.begin && offset < range.end) {
      return true;
    }
  }
  return false;
}

std::optional<std::size_t> select_mutation_offset(const std::vector<ByteRange>& focus_ranges,
                                                  const std::vector<ByteRange>& protect_ranges,
                                                  std::size_t input_size,
                                                  std::mt19937& rng) {
  if (input_size == 0) {
    return std::nullopt;
  }

  for (int attempt = 0; attempt < 32; ++attempt) {
    std::size_t candidate = 0;
    if (!focus_ranges.empty()) {
      std::uniform_int_distribution<std::size_t> range_dist(0, focus_ranges.size() - 1);
      const auto& range = focus_ranges[range_dist(rng)];
      const std::size_t begin = std::min<std::size_t>(range.begin, input_size - 1);
      // Promote to size_t before the +1 so we never wrap a uint32_t at
      // numeric_limits<uint32_t>::max(); the size_t domain has 32 bits
      // of headroom on every supported platform.
      const std::size_t range_end_st = range.end;
      const std::size_t bumped_end =
          static_cast<std::size_t>(range.begin) + std::size_t{1};
      const std::size_t end = std::min<std::size_t>(
          std::max<std::size_t>(range_end_st, bumped_end), input_size);
      if (end <= begin) {
        continue;
      }
      std::uniform_int_distribution<std::size_t> offset_dist(begin, end - 1);
      candidate = offset_dist(rng);
    } else {
      std::uniform_int_distribution<std::size_t> offset_dist(0, input_size - 1);
      candidate = offset_dist(rng);
    }
    if (!offset_in_ranges(candidate, protect_ranges)) {
      return candidate;
    }
  }

  for (std::size_t candidate = 0; candidate < input_size; ++candidate) {
    if (!offset_in_ranges(candidate, protect_ranges)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::vector<ByteRange> normalize_ranges(std::vector<ByteRange> ranges) {
  ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                              [](const ByteRange& range) { return range.end <= range.begin; }),
               ranges.end());
  std::sort(ranges.begin(), ranges.end(),
            [](const ByteRange& lhs, const ByteRange& rhs) {
              if (lhs.begin != rhs.begin) {
                return lhs.begin < rhs.begin;
              }
              return lhs.end < rhs.end;
            });

  std::vector<ByteRange> merged;
  for (const auto& range : ranges) {
    if (merged.empty() || range.begin > merged.back().end) {
      merged.push_back(range);
    } else {
      merged.back().end = std::max(merged.back().end, range.end);
    }
  }
  return merged;
}

}  // namespace fuzzpilot

