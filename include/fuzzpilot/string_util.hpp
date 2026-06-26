#pragma once

#include "fuzzpilot/json_utils.hpp"

#include <string>
#include <string_view>

namespace fuzzpilot {

std::string trim(std::string_view value);

std::string format_double(double value);

}  // namespace fuzzpilot
