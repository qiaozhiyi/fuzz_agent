#pragma once

#include <csignal>  // for sig_atomic_t in install_termination_signal_handler
#include <cstddef>
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

struct ProcessCaptureResult {
  bool spawned = false;
  bool exited = false;
  int exit_code = -1;
  bool signaled = false;
  int term_signal = 0;
  std::string output;
  std::string error;
};

ProcessResult spawn_process(const std::string& executable,
                            const std::vector<std::string>& argv,
                            const std::map<std::string, std::string>& env);

ProcessCaptureResult run_process_capture(const std::string& executable,
                                         const std::vector<std::string>& argv,
                                         const std::map<std::string, std::string>& env,
                                         bool merge_stderr = true,
                                         std::size_t max_output_bytes = 1024 * 1024,
                                         int timeout_ms = -1);

ProcessStatus wait_process(int pid, int timeout_ms);
bool interrupt_process(int pid);
bool terminate_process(int pid);
bool kill_process(int pid);
bool suspend_process(int pid);
bool resume_process(int pid);
// Group-aware termination — sends the signal to the entire process
// group of `pid` (which is the pid itself when spawn_process used
// POSIX_SPAWN_SETPGROUP). Reliably reaps AFL++ children even when AFL
// has forked workers. Returns true if at least one process was signaled.
bool terminate_process_group(int pid);
bool kill_process_group(int pid);
// Install a SIGINT/SIGTERM handler that flips a flag the controller can
// poll. Idempotent — installs once per process. Returns the address of
// the flag, which is set to a non-zero signal number when either
// signal is received. The flag is `volatile sig_atomic_t` which is
// the POSIX guarantee for signal-handler-to-main-thread visibility
// (std::atomic<int> would also work but is not async-signal-safe to
// use inside the handler on all libc implementations). Callers read
// it with a relaxed load — no further synchronization is needed
// because the flag is set monotonically (0 -> signum, never back).
volatile sig_atomic_t* install_termination_signal_handler();

}  // namespace fuzzpilot
