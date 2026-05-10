#pragma once

#include "fuzzpilot/mutation/strategy.hpp"

#include <cstddef>
#include <optional>
#include <random>
#include <vector>

namespace fuzzpilot {

bool offset_in_ranges(std::size_t offset, const std::vector<ByteRange>& ranges);

std::optional<std::size_t> select_mutation_offset(const std::vector<ByteRange>& focus_ranges,
                                                  const std::vector<ByteRange>& protect_ranges,
                                                  std::size_t input_size,
                                                  std::mt19937& rng);

std::vector<ByteRange> normalize_ranges(std::vector<ByteRange> ranges);

}  // namespace fuzzpilot

