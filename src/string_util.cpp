#include "fuzzpilot/string_util.hpp"

#include <cctype>

namespace fuzzpilot {

// Performance optimization: Uses std::string_view to avoid copying the input
// string. Only creates a new string at the very end after calculating the exact
// trimmed boundaries.
std::string trim(std::string_view value) {
  auto start = value.begin();
  while (start != value.end() &&
         std::isspace(static_cast<unsigned char>(*start))) {
    ++start;
  }

  auto end = value.end();
  while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  return std::string(start, end);
}

// Performance optimization: formats a double without trailing zeros
// to avoid std::ostringstream overhead.
std::string format_double(double value) {
  std::string str = std::to_string(value);
  str.erase(str.find_last_not_of('0') + 1, std::string::npos);
  if (!str.empty() && str.back() == '.') {
    str.push_back('0');
  }
  return str;
}

} // namespace fuzzpilot
