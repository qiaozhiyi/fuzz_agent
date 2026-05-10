#pragma once

#include <map>
#include <string>
#include <vector>

namespace fuzzpilot {

struct ProcessResult {
  int pid = -1;
  std::string error;
};

ProcessResult spawn_process(const std::string& executable,
                            const std::vector<std::string>& argv,
                            const std::map<std::string, std::string>& env);

}  // namespace fuzzpilot

