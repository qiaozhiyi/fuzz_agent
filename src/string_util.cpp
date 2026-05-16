#include "fuzzpilot/string_util.hpp"

#include <algorithm>
#include <cctype>

namespace fuzzpilot {

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

std::string_view trim(std::string_view value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto start = std::find_if(value.begin(), value.end(), not_space);
  if (start == value.end()) {
    return {};
  }
  auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
  return std::string_view(start, std::distance(start, end));
}

}  // namespace fuzzpilot
