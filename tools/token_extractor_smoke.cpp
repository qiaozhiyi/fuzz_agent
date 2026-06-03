#include "fuzzpilot/mutation/token_extractor.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cassert>

int main() {
  // Test 1: Simple JSON entries array
  std::string json1 = R"({
    "agent": "DictionaryAgent",
    "entries": [
      {
        "value": "0x3C21444F4354595045",
        "type": "magic"
      },
      {
        "value": "0x5B43444154415B",
        "type": "magic"
      }
    ]
  })";

  auto tokens1 = fuzzpilot::extract_dictionary_tokens_from_proposal(json1);
  if (tokens1.size() != 2 || tokens1[0] != "<!DOCTYPE" || tokens1[1] != "[CDATA[") {
    std::cerr << "Test 1 failed: size=" << tokens1.size() << "\n";
    return 1;
  }

  // Test 2: direct dictionary_tokens list
  std::string json2 = R"({
    "dictionary_tokens": ["0x3C3F", "plain_text", "0x266C743B"]
  })";
  auto tokens2 = fuzzpilot::extract_dictionary_tokens_from_proposal(json2);
  if (tokens2.size() != 3 || tokens2[0] != "<?" || tokens2[1] != "plain_text" || tokens2[2] != "&lt;") {
    std::cerr << "Test 2 failed: size=" << tokens2.size() << "\n";
    return 2;
  }

  // Test 3: Truncated JSON
  std::string json3 = R"({
    "entries": [
      {
        "value": "0x3C2F"
      })";
  auto tokens3 = fuzzpilot::extract_dictionary_tokens_from_proposal(json3);
  if (tokens3.size() != 1 || tokens3[0] != "</") {
    std::cerr << "Test 3 failed: size=" << tokens3.size() << "\n";
    return 3;
  }

  // Test 4: Check limit and uniqueness
  std::string json4 = R"({
    "dictionary_tokens": ["abc", "abc", "0x616263", "def"]
  })";
  auto tokens4 = fuzzpilot::extract_dictionary_tokens_from_proposal(json4);
  if (tokens4.size() != 2 || tokens4[0] != "abc" || tokens4[1] != "def") {
    std::cerr << "Test 4 failed: size=" << tokens4.size() << "\n";
    return 4;
  }

  // Test 5: Reject control characters
  std::string json5 = R"({
    "dictionary_tokens": ["a\nb", "a\x00b", "abc"]
  })";
  auto tokens5 = fuzzpilot::extract_dictionary_tokens_from_proposal(json5);
  if (tokens5.size() != 1 || tokens5[0] != "abc") {
    std::cerr << "Test 5 failed: size=" << tokens5.size() << "\n";
    return 5;
  }

  // Test 6: Load from dict
  std::filesystem::path dict_path = "smoke_test.dict";
  std::ofstream out(dict_path);
  out << "# test comment\n";
  out << "\"hello\"\n";
  out << "some_key=\"world\"\n";
  out << "\"escaped \\\\ \\\" \\x41\"\n";
  out.close();

  auto dict_tokens = fuzzpilot::load_tokens_from_dict(dict_path);
  std::filesystem::remove(dict_path);

  if (dict_tokens.size() != 3 || dict_tokens[0] != "hello" || dict_tokens[1] != "world" || dict_tokens[2] != "escaped \\ \" A") {
    std::cerr << "Test 6 failed: size=" << dict_tokens.size() << "\n";
    for (const auto& t : dict_tokens) {
       std::cerr << "  Token: [" << t << "]\n";
    }
    return 6;
  }

  std::cout << "token extractor smoke passed\n";
  return 0;
}
