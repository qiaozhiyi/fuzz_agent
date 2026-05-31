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

  std::string s;
  // Estimate capacity: prefix + "_" + 20 (micros) + "_p" + 10 (pid) + "_" + 4
  // (counter)
  s.reserve(prefix.size() + 1 + 20 + 2 + 10 + 1 + 4);
  s += prefix;
  s += "_";
  s += std::to_string(micros);
  s += "_p";
  s += std::to_string(current_process_id());
  s += "_";

  const auto c = counter.fetch_add(1, std::memory_order_relaxed);
  std::string c_str = std::to_string(c);
  if (c_str.size() < 4) {
    s.append(4 - c_str.size(), '0');
  }
  s += c_str;
  return s;
}

} // namespace fuzzpilot
