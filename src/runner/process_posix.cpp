#include "fuzzpilot/runner/process.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace fuzzpilot {

ProcessResult spawn_process(const std::string& executable,
                            const std::vector<std::string>& argv,
                            const std::map<std::string, std::string>& env) {
  std::vector<std::string> env_storage;
  for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
    env_storage.emplace_back(*current);
  }
  for (const auto& [key, value] : env) {
    env_storage.push_back(key + "=" + value);
  }

  std::vector<char*> argv_raw;
  argv_raw.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    argv_raw.push_back(const_cast<char*>(arg.c_str()));
  }
  argv_raw.push_back(nullptr);

  std::vector<char*> env_raw;
  env_raw.reserve(env_storage.size() + 1);
  for (auto& item : env_storage) {
    env_raw.push_back(item.data());
  }
  env_raw.push_back(nullptr);

  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, executable.c_str(), nullptr, nullptr,
                              argv_raw.data(), env_raw.data());
  if (rc != 0) {
    return {.pid = -1, .error = std::strerror(rc)};
  }
  return {.pid = static_cast<int>(pid), .error = {}};
}

ProcessStatus wait_process(int pid, int timeout_ms) {
  ProcessStatus status;
  if (pid <= 0) return status;

  int wstatus = 0;
  if (timeout_ms < 0) {
    if (waitpid(pid, &wstatus, 0) == pid) {
      status.exited = WIFEXITED(wstatus);
      status.exit_code = WEXITSTATUS(wstatus);
      status.signaled = WIFSIGNALED(wstatus);
      status.term_signal = WTERMSIG(wstatus);
    }
    return status;
  }

  const int poll_interval_ms = 50;
  int elapsed = 0;
  while (elapsed <= timeout_ms) {
    const pid_t result = waitpid(pid, &wstatus, WNOHANG);
    if (result == pid) {
      status.exited = WIFEXITED(wstatus);
      status.exit_code = WEXITSTATUS(wstatus);
      status.signaled = WIFSIGNALED(wstatus);
      status.term_signal = WTERMSIG(wstatus);
      return status;
    }
    if (result == -1) {
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    elapsed += poll_interval_ms;
  }
  return status;
}

bool kill_process(int pid) {
  if (pid <= 0) return false;
  return kill(pid, SIGKILL) == 0;
}

bool suspend_process(int pid) {
  if (pid <= 0) return false;
  return kill(pid, SIGSTOP) == 0;
}

bool resume_process(int pid) {
  if (pid <= 0) return false;
  return kill(pid, SIGCONT) == 0;
}

}  // namespace fuzzpilot
