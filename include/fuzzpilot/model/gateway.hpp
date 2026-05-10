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
};

struct ModelResponse {
  std::string provider;
  std::string model;
  std::string request_id;
  std::string response_json;
  std::string context_hash;
  std::string response_hash;
  uint64_t latency_ms = 0;
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
