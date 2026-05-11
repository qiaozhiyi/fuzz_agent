#include "fuzzpilot/runner/process.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr const char* kEnvName = "FUZZPILOT_PROCESS_CAPTURE_SMOKE";

std::string strip_trailing_newline(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--child-print-env") {
    const char* value = std::getenv(kEnvName);
    if (value != nullptr) {
      std::cout << value;
    }
    return 0;
  }

  setenv(kEnvName, "old", 1);
  const std::string self = argv[0];
  const auto result = fuzzpilot::run_process_capture(
      self, {self, "--child-print-env"}, {{kEnvName, "new"}}, true);

  if (!result.spawned || !result.exited || result.exit_code != 0) {
    std::cerr << "process capture failed: " << result.error << "\n";
    return 1;
  }

  const auto output = strip_trailing_newline(result.output);
  if (output != "new") {
    std::cerr << "environment override did not win; output=" << output << "\n";
    return 2;
  }

  std::cout << "process capture smoke passed\n";
  return 0;
}
