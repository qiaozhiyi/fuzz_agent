#include "fuzzpilot/model/gateway.hpp"

#include <iostream>

int main() {
  const std::string glm_401 =
      "{\"error\":{\"code\":\"401\",\"message\":\"令牌已过期或验证不正确\"}}\n"
      "HTTP_STATUS:401";
  if (fuzzpilot::classify_openai_compatible_http_error(glm_401, 401) != "auth_error") {
    std::cerr << "GLM HTTP 401 was not classified as auth_error\n";
    return 1;
  }
  const std::string rate_limited =
      "{\"error\":{\"message\":\"too many requests\"}}\nHTTP_STATUS:429";
  if (fuzzpilot::classify_openai_compatible_http_error(rate_limited, 429) != "rate_limit") {
    std::cerr << "HTTP 429 was not classified as rate_limit\n";
    return 2;
  }
  std::cout << "model gateway smoke passed\n";
  return 0;
}
