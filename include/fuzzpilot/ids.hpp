#pragma once

#include <atomic>
#include <chrono>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fuzzpilot {

inline unsigned long long current_process_id() {
#if defined(_WIN32)
  return static_cast<unsigned long long>(_getpid());
#else
  return static_cast<unsigned long long>(getpid());
#endif
}

inline std::string make_id(const std::string& prefix) {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  const auto pid = current_process_id();
  const auto cnt = counter.fetch_add(1, std::memory_order_relaxed);

  // Performance optimization: Avoid std::ostringstream overhead for simple
  // string formatting in this frequently called ID generation function.
  // Pre-allocate std::string and use standard concatenation/to_string.
  std::string cnt_str = std::to_string(cnt);
  if (cnt_str.length() < 4) {
    cnt_str.insert(0, 4 - cnt_str.length(), '0');
  }

  std::string micros_str = std::to_string(micros);
  std::string pid_str = std::to_string(pid);

  std::string out;
  out.reserve(prefix.length() + 1 + micros_str.length() + 2 + pid_str.length() + 1 + cnt_str.length());
  out += prefix;
  out += '_';
  out += micros_str;
  out += "_p";
  out += pid_str;
  out += '_';
  out += cnt_str;

  return out;
}

}  // namespace fuzzpilot
