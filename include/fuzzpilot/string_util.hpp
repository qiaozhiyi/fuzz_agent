#ifndef FUZZPILOT_STRING_UTIL_HPP
#define FUZZPILOT_STRING_UTIL_HPP

#include <string>
#include <string_view>

namespace fuzzpilot {

// Optimizes JSON string escaping by avoiding std::ostringstream allocations
// and using std::string::reserve.
std::string json_escape(std::string_view value);

// Returns a view into the given string_view, trimming leading and trailing whitespace.
// Avoids memory allocations completely.
std::string_view trim(std::string_view value);

}  // namespace fuzzpilot

#endif  // FUZZPILOT_STRING_UTIL_HPP
