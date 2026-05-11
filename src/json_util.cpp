#include "fuzzpilot/json_util.hpp"

namespace fuzzpilot {

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + value.size() / 8);
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

} // namespace fuzzpilot
