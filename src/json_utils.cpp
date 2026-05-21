#include "fuzzpilot/json_utils.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

namespace fuzzpilot {
namespace {

std::size_t first_nonspace(std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!std::isspace(static_cast<unsigned char>(text[i]))) {
      return i;
    }
  }
  return std::string_view::npos;
}

bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  return 10 + (c - 'A');
}

class JsonParser {
 public:
  // Hard cap on object/array nesting. Real production JSON rarely
  // exceeds 10 levels; a 64-level limit is generous but still rejects
  // adversarial payloads that try to exhaust the parser stack. The
  // limit applies to parse_value's recursion depth, which is how
  // nested object/array recursion is bounded.
  static constexpr int kMaxDepth = 64;

  explicit JsonParser(std::string_view text) : text_(text) {}

  bool parse_document_value() {
    skip_space();
    if (pos_ >= text_.size() || (text_[pos_] != '{' && text_[pos_] != '[')) {
      return false;
    }
    if (!parse_value()) {
      return false;
    }
    skip_space();
    return pos_ == text_.size();
  }

  bool parse_document_string_array(std::vector<std::string>& values) {
    skip_space();
    if (!consume('[')) {
      return false;
    }
    skip_space();
    if (consume(']')) {
      skip_space();
      return pos_ == text_.size();
    }

    while (true) {
      std::string value;
      if (!parse_string(&value)) {
        return false;
      }
      values.push_back(std::move(value));
      skip_space();
      if (consume(']')) {
        skip_space();
        return pos_ == text_.size();
      }
      if (!consume(',')) {
        return false;
      }
      skip_space();
    }
  }

  bool parse_top_level_object_keys(std::vector<std::string>& keys) {
    skip_space();
    if (!consume('{')) {
      return false;
    }
    skip_space();
    if (consume('}')) {
      skip_space();
      return pos_ == text_.size();
    }

    while (true) {
      std::string key;
      if (!parse_string(&key)) {
        return false;
      }
      skip_space();
      if (!consume(':')) {
        return false;
      }
      keys.push_back(std::move(key));
      if (!parse_value()) {
        return false;
      }
      skip_space();
      if (consume('}')) {
        skip_space();
        return pos_ == text_.size();
      }
      if (!consume(',')) {
        return false;
      }
      skip_space();
    }
  }

  std::optional<std::string> extract_top_level_member_value(std::string_view needle) {
    skip_space();
    if (!consume('{')) {
      return std::nullopt;
    }
    skip_space();
    if (consume('}')) {
      skip_space();
      return std::nullopt;
    }

    while (true) {
      std::string key;
      if (!parse_string(&key)) {
        return std::nullopt;
      }
      skip_space();
      if (!consume(':')) {
        return std::nullopt;
      }
      skip_space();
      const std::size_t value_start = pos_;
      if (!parse_value()) {
        return std::nullopt;
      }
      const std::size_t value_end = pos_;
      if (key == needle) {
        return std::string(text_.substr(value_start, value_end - value_start));
      }
      skip_space();
      if (consume('}')) {
        skip_space();
        return std::nullopt;
      }
      if (!consume(',')) {
        return std::nullopt;
      }
      skip_space();
    }
  }

 private:
  void skip_space() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  bool consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool parse_value() {
    skip_space();
    if (pos_ >= text_.size()) {
      return false;
    }
    // Depth guard — protects against adversarial deeply-nested input
    // (e.g. an LLM returning 5000 nested arrays would otherwise blow
    // the C++ recursion stack on most platforms).
    if (depth_ >= kMaxDepth) {
      return false;
    }
    const char c = text_[pos_];
    if (c == '{') {
      ++depth_;
      const bool ok = parse_object();
      --depth_;
      return ok;
    }
    if (c == '[') {
      ++depth_;
      const bool ok = parse_array();
      --depth_;
      return ok;
    }
    if (c == '"') {
      return parse_string(nullptr);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return parse_number();
    }
    if (text_.substr(pos_, 4) == "true") {
      pos_ += 4;
      return true;
    }
    if (text_.substr(pos_, 5) == "false") {
      pos_ += 5;
      return true;
    }
    if (text_.substr(pos_, 4) == "null") {
      pos_ += 4;
      return true;
    }
    return false;
  }

  bool parse_object() {
    if (!consume('{')) {
      return false;
    }
    skip_space();
    if (consume('}')) {
      return true;
    }

    while (true) {
      if (!parse_string(nullptr)) {
        return false;
      }
      skip_space();
      if (!consume(':')) {
        return false;
      }
      if (!parse_value()) {
        return false;
      }
      skip_space();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
      skip_space();
    }
  }

  bool parse_array() {
    if (!consume('[')) {
      return false;
    }
    skip_space();
    if (consume(']')) {
      return true;
    }

    while (true) {
      if (!parse_value()) {
        return false;
      }
      skip_space();
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
      skip_space();
    }
  }

  bool parse_string(std::string* out) {
    if (!consume('"')) {
      return false;
    }

    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') {
        return true;
      }
      if (c < 0x20) {
        return false;
      }
      if (c != '\\') {
        if (out != nullptr) {
          out->push_back(static_cast<char>(c));
        }
        continue;
      }

      if (pos_ >= text_.size()) {
        return false;
      }
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          if (out != nullptr) {
            out->push_back(escaped);
          }
          break;
        case 'b':
          if (out != nullptr) {
            out->push_back('\b');
          }
          break;
        case 'f':
          if (out != nullptr) {
            out->push_back('\f');
          }
          break;
        case 'n':
          if (out != nullptr) {
            out->push_back('\n');
          }
          break;
        case 'r':
          if (out != nullptr) {
            out->push_back('\r');
          }
          break;
        case 't':
          if (out != nullptr) {
            out->push_back('\t');
          }
          break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            return false;
          }
          int codepoint = 0;
          for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_ + static_cast<std::size_t>(i)];
            if (!is_hex_digit(h)) {
              return false;
            }
            codepoint = (codepoint << 4) | hex_value(h);
          }
          pos_ += 4;
          if (out != nullptr) {
            out->push_back(codepoint <= 0x7f ? static_cast<char>(codepoint) : '?');
          }
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool parse_number() {
    if (consume('-') && pos_ >= text_.size()) {
      return false;
    }

    if (consume('0')) {
      if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        return false;
      }
    } else {
      if (pos_ >= text_.size() || text_[pos_] < '1' || text_[pos_] > '9') {
        return false;
      }
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }

    if (consume('.')) {
      if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        return false;
      }
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }

    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        return false;
      }
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }

    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  // Current nesting depth of object/array recursion; bounded by
  // kMaxDepth. Reset implicitly because we only use one parser per
  // call site.
  int depth_ = 0;
};

bool has_key(const std::vector<std::string>& keys, std::string_view key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

std::vector<std::string> extract_required_schema_keys(std::string_view schema_json) {
  const auto required = JsonParser(schema_json).extract_top_level_member_value("required");
  if (!required) {
    return {};
  }

  std::vector<std::string> keys;
  JsonParser parser(*required);
  if (!parser.parse_document_string_array(keys)) {
    return {};
  }
  return keys;
}

}  // namespace

std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          constexpr char kHex[] = "0123456789abcdef";
          out.push_back(kHex[(c >> 4) & 0x0f]);
          out.push_back(kHex[c & 0x0f]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

std::string json_value_or_raw(std::string_view value) {
  if (value.empty()) {
    return "{}";
  }
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first != std::string_view::npos && (value[first] == '{' || value[first] == '[') &&
      is_complete_json_value(value)) {
    return std::string(value);
  }
  return std::string("{\"raw\":\"") + json_escape(value) + "\"}";
}

bool is_complete_json_value(std::string_view text) {
  JsonParser parser(text);
  return parser.parse_document_value();
}

// Removed `is_agent_proposal_json` — dead code with a hardcoded schema
// `{agent, interventions, seed_strategies}` that would reject the
// ResultAnalysisAgent shape. Schema validation is now per-task via
// the gateway + per-call guardrail in agents/agent_runtime.cpp.

bool json_object_satisfies_required_schema(std::string_view object_json,
                                           std::string_view schema_json) {
  const auto start = first_nonspace(object_json);
  if (start == std::string_view::npos || object_json[start] != '{') {
    return false;
  }
  std::vector<std::string> top_level_keys;
  JsonParser parser(object_json);
  if (!parser.parse_top_level_object_keys(top_level_keys)) {
    return false;
  }

  auto required_keys = extract_required_schema_keys(schema_json);
  if (required_keys.empty()) {
    required_keys.push_back("agent");
  }
  return std::all_of(required_keys.begin(), required_keys.end(),
                     [&](const std::string& key) { return has_key(top_level_keys, key); });
}

std::optional<std::string> extract_top_level_json_value(std::string_view object_json,
                                                        std::string_view key) {
  JsonParser parser(object_json);
  return parser.extract_top_level_member_value(key);
}

std::vector<std::string> extract_string_array_field(std::string_view object_json,
                                                    std::string_view key) {
  // Reuse the real JSON parser instead of a hand-rolled string scan.
  // The previous hand-rolled scanner mishandled \uXXXX escapes and
  // could swallow the last array element when the closing quote was
  // missing. JsonParser already understands the full JSON grammar
  // including unicode escapes and depth-limited nesting.
  std::vector<std::string> result;
  auto raw = extract_top_level_json_value(object_json, key);
  if (!raw) {
    return result;
  }
  JsonParser inner_parser(*raw);
  inner_parser.parse_document_string_array(result);
  return result;
}

}  // namespace fuzzpilot
