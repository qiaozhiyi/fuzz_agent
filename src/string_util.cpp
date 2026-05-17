#include <cctype>
#include "fuzzpilot/string_util.hpp"

namespace fuzzpilot {

std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + value.size() / 10); // Heuristic reservation
  for (const char c : value) {
    switch (c) {
      case '\\': out.append("\\\\"); break;
      case '"': out.append("\\\""); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

std::string_view trim(std::string_view value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }

  if (start == value.size()) {
    return std::string_view();
  }

  size_t end = value.size() - 1;
  while (end > start && std::isspace(static_cast<unsigned char>(value[end]))) {
    --end;
  }

  return value.substr(start, end - start + 1);
}

} // namespace fuzzpilot
