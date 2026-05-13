#pragma once

#include <string>
#include <string_view>
#include <cctype>

namespace fuzzpilot {

// ⚡ Bolt Optimization:
// Changed trim to accept std::string_view instead of passing std::string by value.
// This prevents unnecessary heap allocations when trimming strings,
// improving string processing performance significantly.
inline std::string trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

} // namespace fuzzpilot
