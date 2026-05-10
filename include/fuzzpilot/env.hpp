#pragma once

#include <string>

namespace fuzzpilot {

struct EnvSnapshot {
  std::string os;
  std::string arch;
  std::string kernel;
  std::string afl_version;
  std::string compiler_version;
};

EnvSnapshot capture_env_snapshot(const std::string& afl_fuzz_path);
std::string env_snapshot_json(const EnvSnapshot& snapshot);

}  // namespace fuzzpilot

