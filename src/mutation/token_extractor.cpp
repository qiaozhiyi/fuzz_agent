#include "fuzzpilot/mutation/token_extractor.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fuzzpilot {
namespace {

bool is_hex_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_to_int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return 10 + (c - 'A');
}

std::string decode_hex_string(const std::string& hex) {
  std::string s = hex;
  if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
    s = s.substr(2);
  }
  
  // Remove any whitespace inside just in case
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
    return std::isspace(c);
  }), s.end());

  std::string bytes;
  bytes.reserve(s.size() / 2);
  for (size_t i = 0; i + 1 < s.size(); i += 2) {
    if (is_hex_char(s[i]) && is_hex_char(s[i + 1])) {
      char val = static_cast<char>((hex_to_int(s[i]) << 4) | hex_to_int(s[i + 1]));
      bytes.push_back(val);
    } else {
      break;
    }
  }
  return bytes;
}

std::string unescape_dict_string(const std::string& s) {
  std::string res;
  res.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[i + 1];
      if (c == 'x' && i + 3 < s.size()) {
        std::string hex_str = s.substr(i + 2, 2);
        try {
          int val = std::stoi(hex_str, nullptr, 16);
          res.push_back(static_cast<char>(val));
        } catch (...) {
          res.push_back('?');
        }
        i += 3;
      } else if (c == 'n') {
        res.push_back('\n');
        i += 1;
      } else if (c == 'r') {
        res.push_back('\r');
        i += 1;
      } else if (c == 't') {
        res.push_back('\t');
        i += 1;
      } else {
        res.push_back(c);
        i += 1;
      }
    } else {
      res.push_back(s[i]);
    }
  }
  return res;
}

bool is_valid_token(const std::string& token) {
  if (token.empty() || token.size() > 4096) return false;
  for (char c : token) {
    if (c == '\n' || c == '\r' || c == '\0') {
      return false;
    }
  }
  return true;
}

}  // namespace

std::vector<std::string> extract_dictionary_tokens_from_proposal(
    const std::string& proposal_json) {
  std::vector<std::string> tokens;
  std::set<std::string> unique_tokens;

  size_t i = 0;
  std::string last_seen_key = "";
  bool in_token_array = false;

  while (i < proposal_json.size()) {
    char c = proposal_json[i];

    if (c == ']' && in_token_array) {
      in_token_array = false;
      i++;
      continue;
    }

    if (c == '"') {
      std::string str_val;
      i++; // skip open quote
      while (i < proposal_json.size() && proposal_json[i] != '"') {
        if (proposal_json[i] == '\\' && i + 1 < proposal_json.size()) {
          char esc = proposal_json[i + 1];
          if (esc == 'n') str_val.push_back('\n');
          else if (esc == 'r') str_val.push_back('\r');
          else if (esc == 't') str_val.push_back('\t');
          else if (esc == 'x' && i + 3 < proposal_json.size()) {
            std::string hex_str = proposal_json.substr(i + 2, 2);
            try {
              int val = std::stoi(hex_str, nullptr, 16);
              str_val.push_back(static_cast<char>(val));
            } catch (...) {
              str_val.push_back('?');
            }
            i += 2;
          } else {
            str_val.push_back(esc);
          }
          i += 2;
        } else {
          str_val.push_back(proposal_json[i]);
          i++;
        }
      }
      if (i < proposal_json.size()) {
        i++; // skip close quote
      }

      // Peek at next non-whitespace char
      size_t next_non_ws = i;
      while (next_non_ws < proposal_json.size() &&
             std::isspace(static_cast<unsigned char>(proposal_json[next_non_ws]))) {
        next_non_ws++;
      }

      if (next_non_ws < proposal_json.size() && proposal_json[next_non_ws] == ':') {
        // It is a key
        last_seen_key = str_val;
        i = next_non_ws + 1; // skip ':'

        // Check if array starts
        size_t array_peek = i;
        while (array_peek < proposal_json.size() &&
               std::isspace(static_cast<unsigned char>(proposal_json[array_peek]))) {
          array_peek++;
        }
        if (array_peek < proposal_json.size() && proposal_json[array_peek] == '[') {
          if (last_seen_key == "dictionary_tokens" ||
              last_seen_key == "magic_values" ||
              last_seen_key == "values") {
            in_token_array = true;
            i = array_peek + 1; // skip '['
          }
        }
      } else {
        // It is a value
        std::string candidate = str_val;
        if (candidate.rfind("0x", 0) == 0 || candidate.rfind("0X", 0) == 0) {
          candidate = decode_hex_string(candidate);
        }

        bool is_token = false;
        if (last_seen_key == "value" ||
            last_seen_key == "magic_value" ||
            last_seen_key == "value_raw") {
          is_token = true;
        } else if (in_token_array) {
          is_token = true;
        } else if (str_val.rfind("0x", 0) == 0 || str_val.rfind("0X", 0) == 0) {
          is_token = true;
        }

        if (is_token && is_valid_token(candidate)) {
          if (unique_tokens.find(candidate) == unique_tokens.end()) {
            unique_tokens.insert(candidate);
            tokens.push_back(candidate);
          }
        }
      }
      continue;
    }

    i++;
  }

  if (tokens.size() > 256) {
    tokens.resize(256);
  }
  return tokens;
}

std::vector<std::string> load_tokens_from_dict(
    const std::filesystem::path& dict_path) {
  std::vector<std::string> tokens;
  std::set<std::string> unique_tokens;

  std::ifstream infile(dict_path);
  if (!infile.is_open()) {
    return tokens;
  }

  std::string line;
  while (std::getline(infile, line)) {
    // Trim leading whitespace
    size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
      start++;
    }
    if (start >= line.size() || line[start] == '#') {
      continue;
    }

    // Find quotes
    size_t first_quote = line.find('"', start);
    if (first_quote == std::string::npos) {
      continue;
    }
    size_t last_quote = line.rfind('"');
    if (last_quote == std::string::npos || last_quote <= first_quote) {
      continue;
    }

    std::string escaped = line.substr(first_quote + 1, last_quote - first_quote - 1);
    std::string token = unescape_dict_string(escaped);
    if (is_valid_token(token)) {
      if (unique_tokens.find(token) == unique_tokens.end()) {
        unique_tokens.insert(token);
        tokens.push_back(token);
      }
    }
  }

  return tokens;
}

}  // namespace fuzzpilot
