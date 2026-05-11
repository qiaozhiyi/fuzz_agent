#include "fuzzpilot/runner/process.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace fuzzpilot {
namespace {

std::vector<std::string> build_env_storage(const std::map<std::string, std::string>& env) {
  std::vector<std::string> env_storage;
  for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
    env_storage.emplace_back(*current);
  }
  for (const auto& [key, value] : env) {
    env_storage.push_back(key + "=" + value);
  }
  return env_storage;
}

std::vector<char*> build_env_raw(std::vector<std::string>& env_storage) {
  std::vector<char*> env_raw;
  env_raw.reserve(env_storage.size() + 1);
  for (auto& item : env_storage) {
    env_raw.push_back(item.data());
  }
  env_raw.push_back(nullptr);
  return env_raw;
}

std::vector<char*> build_argv_raw(const std::vector<std::string>& argv) {
  std::vector<char*> argv_raw;
  argv_raw.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    argv_raw.push_back(const_cast<char*>(arg.c_str()));
  }
  argv_raw.push_back(nullptr);
  return argv_raw;
}

}  // namespace

ProcessResult spawn_process(const std::string& executable,
                            const std::vector<std::string>& argv,
                            const std::map<std::string, std::string>& env) {
  auto env_storage = build_env_storage(env);
  auto env_raw = build_env_raw(env_storage);
  auto argv_raw = build_argv_raw(argv);

  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, executable.c_str(), nullptr, nullptr,
                              argv_raw.data(), env_raw.data());
  if (rc != 0) {
    return {.pid = -1, .error = std::strerror(rc)};
  }
  return {.pid = static_cast<int>(pid), .error = {}};
}

ProcessCaptureResult run_process_capture(const std::string& executable,
                                         const std::vector<std::string>& argv,
                                         const std::map<std::string, std::string>& env,
                                         bool merge_stderr) {
  ProcessCaptureResult result;
  int pipe_fd[2] = {-1, -1};
  if (pipe(pipe_fd) != 0) {
    result.error = std::strerror(errno);
    return result;
  }

  auto env_storage = build_env_storage(env);
  auto env_raw = build_env_raw(env_storage);
  auto argv_raw = build_argv_raw(argv);

  posix_spawn_file_actions_t actions;
  int rc = posix_spawn_file_actions_init(&actions);
  if (rc != 0) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    result.error = std::strerror(rc);
    return result;
  }

  posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
  posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
  if (merge_stderr) {
    posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO);
  }
  posix_spawn_file_actions_addclose(&actions, pipe_fd[1]);

  pid_t pid = -1;
  rc = posix_spawnp(&pid, executable.c_str(), &actions, nullptr,
                    argv_raw.data(), env_raw.data());
  posix_spawn_file_actions_destroy(&actions);
  close(pipe_fd[1]);

  if (rc != 0) {
    close(pipe_fd[0]);
    result.error = std::strerror(rc);
    return result;
  }
  result.spawned = true;

  char buffer[4096];
  while (true) {
    const ssize_t n = read(pipe_fd[0], buffer, sizeof(buffer));
    if (n > 0) {
      result.output.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    result.error = std::strerror(errno);
    break;
  }
  close(pipe_fd[0]);

  int wstatus = 0;
  if (waitpid(pid, &wstatus, 0) == pid) {
    result.exited = WIFEXITED(wstatus);
    if (result.exited) {
      result.exit_code = WEXITSTATUS(wstatus);
    }
    result.signaled = WIFSIGNALED(wstatus);
    if (result.signaled) {
      result.term_signal = WTERMSIG(wstatus);
    }
  }
  return result;
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
