#include "fuzzpilot/string_util.hpp"

#include <cctype>

namespace fuzzpilot {

// Performance optimization: Uses std::string_view to avoid copying the input string.
// Pre-allocates output string capacity using reserve() to minimize dynamic memory reallocations during concatenation.
std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + value.size() / 10);
  for (const char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

// Performance optimization: Uses std::string_view to avoid copying the input string.
// Only creates a new string at the very end after calculating the exact trimmed boundaries.
std::string trim(std::string_view value) {
  auto start = value.begin();
  while (start != value.end() && std::isspace(static_cast<unsigned char>(*start))) {
    ++start;
  }

  auto end = value.end();
  while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  return std::string(start, end);
}

} // namespace fuzzpilot
