#pragma once

#include <map>
#include <string>
#include <vector>

namespace fuzzpilot {

struct ProcessResult {
  int pid = -1;
  std::string error;
};

struct ProcessStatus {
  bool exited = false;
  int exit_code = 0;
  bool signaled = false;
  int term_signal = 0;
};

ProcessResult spawn_process(const std::string& executable,
                            const std::vector<std::string>& argv,
                            const std::map<std::string, std::string>& env);

ProcessStatus wait_process(int pid, int timeout_ms);
bool kill_process(int pid);
bool suspend_process(int pid);
bool resume_process(int pid);

}  // namespace fuzzpilot
