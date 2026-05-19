#pragma once

#include <string>
#include <string_view>

namespace fuzzpilot {

std::string_view trim(std::string_view value);

std::string json_escape(std::string_view value);

}  // namespace fuzzpilot
