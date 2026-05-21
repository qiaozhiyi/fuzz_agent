#pragma once

#include <cstdint>
#include <string>

namespace fuzzpilot {

struct ModelRequest {
  std::string agent_name;
  std::string system_prompt;
  std::string user_context_json;
  std::string output_schema_json;
  uint32_t timeout_ms = 30000;
  uint32_t max_output_tokens = 1024;
  // Optional per-call sampling parameters; default 0.0 / 1.0 keeps replay
  // determinism but the experiment matrix can sweep them.
  double temperature = 0.0;
  double top_p = 1.0;
  // Optional deterministic seed (forwarded to OpenAI-compatible APIs that
  // honour it). 0 means "do not send".
  uint64_t seed = 0;
};

struct ModelResponse {
  std::string provider;
  std::string model;
  std::string request_id;
  std::string response_json;
  // Hashes for replay verification.
  std::string context_hash;
  std::string response_hash;
  // Persisted in full for paper-grade reproducibility. The caller is
  // responsible for logging these to the replay store (in compressed form
  // if size becomes a concern).
  std::string full_request_payload;
  std::string full_raw_response;
  uint64_t latency_ms = 0;
  uint64_t input_tokens = 0;
  uint64_t output_tokens = 0;
  uint32_t retry_count = 0;
  // Coarse error classification — useful for the LLM-failure-rate metric
  // reported in the paper (RQ4). One of:
  //   ok | timeout | http_error | transport_error | parse_error |
  //   schema_invalid | auth_error | guardrail_violation.
  std::string error_kind = "ok";
  bool schema_valid = false;
  std::string error;
};

class IModelGateway {
 public:
  virtual ~IModelGateway() = default;
  virtual ModelResponse complete_json(const ModelRequest& request) = 0;
};

class FakeModelGateway final : public IModelGateway {
 public:
  ModelResponse complete_json(const ModelRequest& request) override;
};

class OpenAICompatibleGateway final : public IModelGateway {
 public:
  OpenAICompatibleGateway(std::string endpoint,
                          std::string model,
                          std::string api_key_env,
                          bool disable_thinking);

  ModelResponse complete_json(const ModelRequest& request) override;

 private:
  std::string endpoint_;
  std::string model_;
  std::string api_key_env_;
  bool disable_thinking_ = true;
};

std::string stable_text_hash(const std::string& text);

}  // namespace fuzzpilot
