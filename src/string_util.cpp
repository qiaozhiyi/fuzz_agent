#include "fuzzpilot/string_util.hpp"
#include <algorithm>
#include <cctype>

namespace fuzzpilot {

std::string_view trim(std::string_view value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto start = std::find_if(value.begin(), value.end(), not_space);
  auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
  if (start >= end) return std::string_view();
  return value.substr(std::distance(value.begin(), start), std::distance(start, end));
}

std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + value.size() / 4);
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

}
