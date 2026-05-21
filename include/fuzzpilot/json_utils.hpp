#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzpilot {

std::string json_escape(std::string_view value);
std::string json_value_or_raw(std::string_view value);
bool is_complete_json_value(std::string_view text);
bool json_object_satisfies_required_schema(std::string_view object_json,
                                           std::string_view schema_json);
std::optional<std::string> extract_top_level_json_value(std::string_view object_json,
                                                        std::string_view key);
// Best-effort extraction of a `"key": ["s1","s2",...]` field from a JSON
// object. Returns an empty vector if the key is missing or malformed.
// Used by the agent guardrail layer to enforce allowed_actions.
std::vector<std::string> extract_string_array_field(std::string_view object_json,
                                                    std::string_view key);

}  // namespace fuzzpilot
