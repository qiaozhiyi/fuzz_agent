#ifndef FUZZPILOT_STRING_UTIL_HPP
#define FUZZPILOT_STRING_UTIL_HPP

#include <string>
#include <string_view>

namespace fuzzpilot {

std::string json_escape(std::string_view value);
std::string_view trim(std::string_view value);

} // namespace fuzzpilot

#endif // FUZZPILOT_STRING_UTIL_HPP
