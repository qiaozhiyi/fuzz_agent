#pragma once

#include <string>
#include <string_view>

namespace fuzzpilot {

std::string json_escape(std::string_view value);
std::string trim(std::string_view value);

} // namespace fuzzpilot
