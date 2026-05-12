#include "fuzzpilot/json_util.hpp"

namespace fuzzpilot {

std::string json_escape(const std::string& value) {
  std::string out;
  // Reserve enough space to avoid reallocations.
  // Add 10% overhead for common escapes, plus a few bytes to avoid small allocs.
  out.reserve(value.size() + value.size() / 10 + 16);

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

}  // namespace fuzzpilot
