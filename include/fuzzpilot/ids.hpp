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

inline std::string make_id(const std::string& prefix) {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

  // Performance optimization: Avoid std::ostringstream allocations
  const auto cnt = counter.fetch_add(1, std::memory_order_relaxed);
  std::string out = prefix;
  out += "_";
  out += std::to_string(micros);
  out += "_p";
  out += std::to_string(current_process_id());
  out += "_";
  std::string cnt_str = std::to_string(cnt);
  if (cnt_str.size() < 4) {
    out.append(4 - cnt_str.size(), '0');
  }
  out += cnt_str;
  return out;
}

}  // namespace fuzzpilot
