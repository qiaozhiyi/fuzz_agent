#include "fuzzpilot/model/gateway.hpp"
#include "fuzzpilot/string_util.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/json_utils.hpp"
#include "fuzzpilot/runner/process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace fuzzpilot {
namespace {

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

// Retained for callers that need to overwrite a known path with 0600
// permission semantics. The hot model-request path now uses
// make_private_tempfile() instead, which is atomic and TOCTOU-safe.
[[maybe_unused]] bool write_private_text_file(const std::filesystem::path& path,
                                              const std::string& content) {
  const int fd = open(path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    return false;
  }
  std::size_t written = 0;
  while (written < content.size()) {
    const ssize_t n = write(fd, content.data() + written, content.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      return false;
    }
    if (n == 0) {
      close(fd);
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return close(fd) == 0;
}

// Create a temp file with an unguessable name via mkstemp(3). The
// returned path is owned by us (mode 0600, created atomically by the
// kernel — no TOCTOU window). On failure returns an empty path. The
// caller must remove the file after use; we also write `content` to
// the freshly-opened fd before closing so callers don't have a second
// open() race.
std::filesystem::path make_private_tempfile(const std::string& prefix,
                                            const std::string& content) {
  auto tmp_dir = std::filesystem::temp_directory_path();
  // Template requires exactly six trailing X's that mkstemp replaces.
  std::string tmpl = (tmp_dir / (prefix + "XXXXXX")).string();
  // mkstemp wants a mutable C-string; copy into a vector so the kernel
  // can patch the X's in place.
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  const int fd = mkstemp(buf.data());
  if (fd < 0) {
    return {};
  }
  // mkstemp creates 0600 already, but be defensive in case umask differs.
  (void)fchmod(fd, S_IRUSR | S_IWUSR);
  std::size_t written = 0;
  while (written < content.size()) {
    const ssize_t n = write(fd, content.data() + written, content.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      unlink(buf.data());
      return {};
    }
    if (n == 0) {
      close(fd);
      unlink(buf.data());
      return {};
    }
    written += static_cast<std::size_t>(n);
  }
  if (close(fd) != 0) {
    unlink(buf.data());
    return {};
  }
  return std::filesystem::path(buf.data());
}

std::string process_capture_text(const ProcessCaptureResult& result) {
  if (!result.output.empty()) {
    return result.output;
  }
  if (!result.error.empty()) {
    return result.error;
  }
  if (result.signaled) {
    return "process terminated by signal " + std::to_string(result.term_signal);
  }
  if (result.exited) {
    return "process exited with code " + std::to_string(result.exit_code) + " without output";
  }
  return "process produced no output";
}

bool is_transient_transport_error(const ProcessCaptureResult& result,
                                  const std::string& raw) {
  if (!result.spawned || !result.exited || result.signaled) {
    return true;
  }
  if (result.exit_code == 0 && !raw.empty()) {
    return false;
  }
  return raw.empty() ||
         raw.find("TLS connect error") != std::string::npos ||
         raw.find("SSL") != std::string::npos ||
         raw.find("timed out") != std::string::npos ||
         raw.find("Timeout") != std::string::npos ||
         raw.find("Could not resolve host") != std::string::npos ||
         raw.find("Failed to connect") != std::string::npos;
}

// Extract a top-level numeric field from a JSON blob. Used to read
// `usage.prompt_tokens` / `usage.completion_tokens` from OpenAI-compatible
// responses. Best-effort: returns 0 if not present or unparseable.
uint64_t extract_uint_field(const std::string& blob, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  auto pos = blob.find(needle);
  if (pos == std::string::npos) {
    return 0;
  }
  pos += needle.size();
  while (pos < blob.size() && std::isspace(static_cast<unsigned char>(blob[pos]))) ++pos;
  const auto start = pos;
  while (pos < blob.size() && std::isdigit(static_cast<unsigned char>(blob[pos]))) ++pos;
  if (start == pos) {
    return 0;
  }
  try {
    return static_cast<uint64_t>(std::stoull(blob.substr(start, pos - start)));
  } catch (...) {
    return 0;
  }
}

// Coarse error classification based on curl exit + raw body keywords.
std::string classify_error(const ProcessCaptureResult& curl, const std::string& raw) {
  if (!curl.spawned) return "spawn_error";
  if (curl.signaled) return "signaled";
  if (!curl.exited) return "transport_error";
  if (curl.exit_code != 0) {
    if (raw.find("timed out") != std::string::npos ||
        raw.find("Timeout") != std::string::npos) return "timeout";
    if (raw.find("Could not resolve") != std::string::npos ||
        raw.find("Failed to connect") != std::string::npos) return "transport_error";
    return "http_error";
  }
  if (raw.find("\"error\"") != std::string::npos) {
    if (raw.find("authentication") != std::string::npos ||
        raw.find("api key") != std::string::npos ||
        raw.find("unauthorized") != std::string::npos) return "auth_error";
    return "http_error";
  }
  return "ok";
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
    response.error_kind = "auth_error";
    response.response_hash = stable_text_hash(response.error);
    return response;
  }

  const char* api_key = std::getenv(api_key_env_.c_str());
  if (api_key == nullptr || api_key[0] == '\0') {
    response.error = "missing API key environment variable: " + api_key_env_;
    response.error_kind = "auth_error";
    response.response_hash = stable_text_hash(response.error);
    return response;
  }

  std::ostringstream payload;
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
  payload << "\"temperature\":" << request.temperature << ",";
  payload << "\"top_p\":" << request.top_p;
  if (request.seed != 0) {
    payload << ",\"seed\":" << request.seed;
  }
  payload << "}";
  const auto payload_str = payload.str();
  // Persist the full payload on the response so the agent runtime can
  // write it to the replay log. The on-disk temp file is removed below.
  response.full_request_payload = payload_str;

  // Atomic temp file creation via mkstemp — unguessable names + 0600
  // permission, no TOCTOU window. The previous predictable
  // `{request_id}_payload.json` scheme allowed a local attacker on a
  // shared host to symlink-hijack the file between write and curl
  // read.
  const auto payload_path = make_private_tempfile("fuzzpilot.payload.", payload_str);
  const auto auth_header_path = make_private_tempfile(
      "fuzzpilot.auth.", std::string("Authorization: Bearer ") + api_key + "\n");
  if (payload_path.empty() || auth_header_path.empty()) {
    response.error = "failed to create private model request tempfiles";
    response.error_kind = "spawn_error";
    response.response_hash = stable_text_hash(response.error);
    std::error_code ec;
    if (!payload_path.empty()) std::filesystem::remove(payload_path, ec);
    if (!auth_header_path.empty()) std::filesystem::remove(auth_header_path, ec);
    return response;
  }

  const std::string max_time = std::to_string(std::max<uint32_t>(1, request.timeout_ms / 1000));

  const std::vector<std::string> argv = {
      "curl",
      "-sS",
      "--connect-timeout", "10",
      "--max-time", max_time,
      "-H", "Content-Type: application/json",
      "-H", "@" + auth_header_path.string(),
      "-d", "@" + payload_path.string(),
      endpoint_,
  };

  auto curl = run_process_capture("curl", argv, {}, true, 1024 * 1024,
                                  static_cast<int>(request.timeout_ms + 2000));
  auto raw = process_capture_text(curl);
  if (is_transient_transport_error(curl, raw)) {
    ++response.retry_count;
    const auto retry = run_process_capture("curl", argv, {}, true, 1024 * 1024,
                                           static_cast<int>(request.timeout_ms + 2000));
    const auto retry_raw = process_capture_text(retry);
    if (!is_transient_transport_error(retry, retry_raw)) {
      // Only adopt the retry when it cleanly succeeded; never concatenate
      // a successful body with a failed one — the result would parse as
      // garbage but might fool the substring schema check.
      curl = retry;
      raw = retry_raw;
    } else if (raw.empty() && !retry_raw.empty()) {
      curl = retry;
      raw = retry_raw;
    }
  }
  std::error_code ec;
  std::filesystem::remove(payload_path, ec);
  std::filesystem::remove(auth_header_path, ec);

  response.full_raw_response = raw;
  response.response_json = extract_content_field(raw);
  response.response_hash = stable_text_hash(response.response_json);
  // OpenAI-compatible servers report usage. Parse best-effort; fall back
  // to a ~4 chars/token approximation when missing (matches OpenAI's own
  // rule of thumb for English-ish content).
  response.input_tokens = extract_uint_field(raw, "prompt_tokens");
  response.output_tokens = extract_uint_field(raw, "completion_tokens");
  if (response.input_tokens == 0) {
    response.input_tokens = (request.system_prompt.size() + request.user_context_json.size()) / 4;
  }
  if (response.output_tokens == 0) {
    response.output_tokens = response.response_json.size() / 4;
  }
  response.error_kind = classify_error(curl, raw);

  // Detect truncation BEFORE running the schema check. The process
  // capture path silently truncates at 1MB and only marks
  // curl.error = "process output truncated"; without this guard, a
  // mid-JSON cut might happen to contain the required top-level keys
  // and pass schema validation while actually being corrupted.
  const bool truncated = curl.error.find("truncated") != std::string::npos;
  const bool transport_ok = curl.spawned && curl.exited && curl.exit_code == 0 && !truncated;
  const bool schema_ok = transport_ok &&
                         json_object_satisfies_required_schema(response.response_json,
                                                               request.output_schema_json) &&
                         raw.find("\"error\"") == std::string::npos;
  response.schema_valid = schema_ok;
  if (!schema_ok) {
    if (truncated) {
      response.error_kind = "schema_invalid_truncated";
      response.error = "response truncated at 1MB cap before schema validation";
    } else if (transport_ok && response.error_kind == "ok") {
      response.error_kind = "schema_invalid";
    }
    if (response.error.empty()) {
      response.error = transport_ok
                           ? "model response did not satisfy required output schema: " +
                                 response.response_json.substr(0, 512)
                           : raw.substr(0, 512);
    }
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

  if (json_object_satisfies_required_schema(
          "{\"agent\":\"Fake\",\"memory_patch\":{},\"critique\":\"ok\"}",
          request.output_schema_json)) {
    response.response_json =
        std::string("{\"agent\":\"") + request.agent_name +
        "\",\"memory_patch\":{\"kind\":\"smoke\",\"confidence\":0.5},"
        "\"critique\":\"fake model result analysis for deterministic smoke test\"}";
  } else {
    response.response_json =
        std::string("{\"agent\":\"") + request.agent_name +
        "\",\"status\":\"ok\",\"interventions\":[{\"action\":\"per_seed_recipe_probe\","
        "\"hypothesis\":\"fake model proposal for deterministic smoke test\","
        "\"expected_signal\":\"new_edges\",\"risk\":\"low\"}],"
        "\"seed_strategies\":[{\"selector\":{\"mode\":\"global\"},"
        "\"operator_weights\":{\"insert_token\":0.4,\"overwrite_range\":0.3,"
        "\"arith\":0.2,\"bit_flip\":0.1}}],"
        "\"memory_patch\":{\"kind\":\"smoke\",\"confidence\":0.5}}";
  }
  response.response_hash = stable_text_hash(response.response_json);
  response.schema_valid = json_object_satisfies_required_schema(response.response_json,
                                                                request.output_schema_json);
  const auto end = std::chrono::steady_clock::now();
  response.latency_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  return response;
}

}  // namespace fuzzpilot
