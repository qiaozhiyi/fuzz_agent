#include "fuzzpilot/string_util.hpp"

#include <cctype>
#include <cstdio>

namespace fuzzpilot {

std::string format_double(double value) {
  char buf[32];
  int n = std::snprintf(buf, sizeof(buf), "%g", value);
  if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
    return std::string(buf, static_cast<std::size_t>(n));
  }
  return std::to_string(value);
}

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

} // namespace fuzzpilot
