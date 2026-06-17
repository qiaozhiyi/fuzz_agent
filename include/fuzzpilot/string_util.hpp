#pragma once

#include "fuzzpilot/json_utils.hpp"

#include <string>
#include <string_view>

namespace fuzzpilot {

std::string trim(std::string_view value);

// Formats a double into a string without trailing zeros, matching %g behavior,
// avoiding the dynamic allocation overhead of std::ostringstream.
std::string format_double(double value);

} // namespace fuzzpilot
