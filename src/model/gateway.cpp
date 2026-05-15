#include "fuzzpilot/model/gateway.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/runner/process.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <string.h>

namespace fuzzpilot {
namespace {

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default: out << c; break;
    }
  }
  return out.str();
}

bool is_valid_env_name(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(value.front());
  if (!(std::isalpha(first) || value.front() == '_')) {
    return false;
  }
  return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_';
  });
}

std::string decode_json_string_at(const std::string& text, std::size_t cursor) {
  std::string out;
  for (; cursor < text.size(); ++cursor) {
    const char c = text[cursor];
    if (c == '"') {
      break;
    }
    if (c == '\\' && cursor + 1 < text.size()) {
      const char next = text[++cursor];
      switch (next) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        default: out.push_back(next); break;
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string extract_content_field(const std::string& response) {
  const std::string needle = "\"content\":\"";
  const auto pos = response.find(needle);
  if (pos == std::string::npos) {
    return response;
  }
  return decode_json_string_at(response, pos + needle.size());
}

}  // namespace

std::string stable_text_hash(const std::string& text) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char c : text) {
    hash ^= static_cast<uint64_t>(c);
    hash *= 1099511628211ull;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

OpenAICompatibleGateway::OpenAICompatibleGateway(std::string endpoint,
                                                 std::string model,
                                                 std::string api_key_env,
                                                 bool disable_thinking)
    : endpoint_(std::move(endpoint)),
      model_(std::move(model)),
      api_key_env_(std::move(api_key_env)),
      disable_thinking_(disable_thinking) {}

ModelResponse OpenAICompatibleGateway::complete_json(const ModelRequest& request) {
  const auto start = std::chrono::steady_clock::now();
  ModelResponse response;
  response.provider = "openai_compatible";
  response.model = model_;
  response.request_id = make_id("model_req");
  response.context_hash = stable_text_hash(request.agent_name + request.user_context_json);

  if (!is_valid_env_name(api_key_env_)) {
    response.error = "invalid API key environment variable name: " + api_key_env_;
    response.response_hash = stable_text_hash(response.error);
    return response;
  }

  const char* api_key = std::getenv(api_key_env_.c_str());
  if (api_key == nullptr) {
    response.error = "missing API key environment variable: " + api_key_env_;
    response.response_hash = stable_text_hash(response.error);
    return response;
  }

  const auto payload_path = std::filesystem::temp_directory_path() /
                            (response.request_id + "_payload.json");
  {
    std::ofstream payload(payload_path);
    payload << "{";
    payload << "\"model\":\"" << json_escape(model_) << "\",";
    if (disable_thinking_) {
      payload << "\"thinking\":{\"type\":\"disabled\"},";
    }
    payload << "\"messages\":[";
    payload << "{\"role\":\"system\",\"content\":\"" << json_escape(request.system_prompt) << "\"},";
    payload << "{\"role\":\"user\",\"content\":\"" << json_escape(request.user_context_json) << "\"}],";
    payload << "\"response_format\":{\"type\":\"json_object\"},";
    payload << "\"max_tokens\":" << request.max_output_tokens << ",";
    payload << "\"temperature\":0.0";
    payload << "}";
  }

  const std::string max_time = std::to_string(std::max<uint32_t>(1, request.timeout_ms / 1000));

  std::string hdr_tmpl = (std::filesystem::temp_directory_path() / "fp_hdr_XXXXXX").string();
  std::vector<char> hdr_buf(hdr_tmpl.begin(), hdr_tmpl.end());
  hdr_buf.push_back('\0');
  int hdr_fd = mkstemp(hdr_buf.data());
  if (hdr_fd == -1) {
    response.error = "failed to create temporary file for authorization header";
    response.response_hash = stable_text_hash(response.error);
    return response;
  }
  std::string hdr_path = hdr_buf.data();
  std::string auth_header = std::string("Authorization: Bearer ") + api_key;
  if (write(hdr_fd, auth_header.c_str(), auth_header.size()) == -1) {
    close(hdr_fd);
    std::filesystem::remove(hdr_path);
    response.error = "failed to write to temporary file for authorization header";
    response.response_hash = stable_text_hash(response.error);
    return response;
  }
  close(hdr_fd);

  const std::vector<std::string> argv = {
      "curl",
      "-sS",
      "--connect-timeout", "10",
      "--max-time", max_time,
      "-H", "Content-Type: application/json",
      "-H", "@" + hdr_path,
      "-d", "@" + payload_path.string(),
      endpoint_,
  };

  const auto curl = run_process_capture("curl", argv, {}, true);
  const auto raw = curl.spawned ? curl.output : curl.error;
  std::error_code ec;
  std::filesystem::remove(payload_path, ec);
  if (!hdr_path.empty()) {
    std::filesystem::remove(hdr_path, ec);
  }

  response.response_json = extract_content_field(raw);
  response.response_hash = stable_text_hash(response.response_json);
  response.schema_valid = curl.spawned && curl.exited && curl.exit_code == 0 &&
                          response.response_json.find('{') != std::string::npos &&
                          response.response_json.find('}') != std::string::npos &&
                          raw.find("\"error\"") == std::string::npos;
  if (!response.schema_valid) {
    response.error = raw.substr(0, 512);
  }
  const auto end = std::chrono::steady_clock::now();
  response.latency_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  return response;
}

ModelResponse FakeModelGateway::complete_json(const ModelRequest& request) {
  const auto start = std::chrono::steady_clock::now();
  ModelResponse response;
  response.provider = "fake";
  response.model = "fake-fuzzpilot-agent";
  response.request_id = make_id("model_req");
  response.context_hash = stable_text_hash(request.agent_name + request.user_context_json);

  response.response_json =
      std::string("{\"agent\":\"") + request.agent_name +
      "\",\"status\":\"ok\",\"interventions\":[{\"action\":\"per_seed_recipe_probe\","
      "\"hypothesis\":\"fake model proposal for deterministic smoke test\","
      "\"expected_signal\":\"new_edges\",\"risk\":\"low\"}],"
      "\"seed_strategies\":[{\"selector\":{\"mode\":\"global\"},"
      "\"operator_weights\":{\"insert_token\":0.4,\"overwrite_range\":0.3,"
      "\"arith\":0.2,\"bit_flip\":0.1}}],"
      "\"memory_patch\":{\"kind\":\"smoke\",\"confidence\":0.5}}";
  response.response_hash = stable_text_hash(response.response_json);
  response.schema_valid = response.response_json.find("\"agent\"") != std::string::npos &&
                          response.response_json.find("\"interventions\"") != std::string::npos;
  const auto end = std::chrono::steady_clock::now();
  response.latency_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  return response;
}

}  // namespace fuzzpilot
