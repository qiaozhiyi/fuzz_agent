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

  std::string out;
  // Pre-allocate enough space to avoid reallocations.
  // prefix length + 64 chars is plenty for micros, pid, and counter.
  out.reserve(prefix.size() + 64);

  out += prefix;
  out += '_';
  out += std::to_string(micros);
  out += "_p";
  out += std::to_string(current_process_id());
  out += '_';

  auto c = counter.fetch_add(1, std::memory_order_relaxed);
  std::string c_str = std::to_string(c);
  // Zero-padding up to 4 characters
  if (c_str.length() < 4) {
    out.append(4 - c_str.length(), '0');
  }
  out += c_str;

  return out;
}

}  // namespace fuzzpilot
