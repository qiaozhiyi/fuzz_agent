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

  std::string out = prefix;
  out.reserve(prefix.size() + 40);
  out += '_';
  out += std::to_string(micros);
  out += "_p";
  out += std::to_string(current_process_id());
  out += '_';

  auto count = counter.fetch_add(1, std::memory_order_relaxed);
  std::string count_str = std::to_string(count);
  if (count_str.length() < 4) {
    out.append(4 - count_str.length(), '0');
  }
  out += count_str;
  return out;
}

}  // namespace fuzzpilot
