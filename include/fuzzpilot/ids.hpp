#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
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

inline std::string make_id(const std::string &prefix) {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  const auto pid = current_process_id();
  const auto c = counter.fetch_add(1, std::memory_order_relaxed);

  std::string result;
  result.reserve(prefix.size() + 40);
  result += prefix;
  result += "_";
  result += std::to_string(micros);
  result += "_p";
  result += std::to_string(pid);
  result += "_";

  std::string c_str = std::to_string(c);
  if (c_str.size() < 4) {
    result.append(4 - c_str.size(), '0');
  }
  result += c_str;

  return result;
}

} // namespace fuzzpilot
