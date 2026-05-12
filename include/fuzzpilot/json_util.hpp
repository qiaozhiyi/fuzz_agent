#pragma once

#include <string>

namespace fuzzpilot {

// Escapes a string for use in JSON payloads.
std::string json_escape(const std::string& value);

}  // namespace fuzzpilot
