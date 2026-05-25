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

inline std::string make_id(const std::string &prefix) {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  const auto cnt = counter.fetch_add(1, std::memory_order_relaxed);

  std::string micros_str = std::to_string(micros);
  std::string pid_str = std::to_string(current_process_id());
  std::string cnt_str = std::to_string(cnt);

  std::size_t pad = 0;
  if (cnt_str.length() < 4) {
    pad = 4 - cnt_str.length();
  }

  std::string out;
  out.reserve(prefix.length() + 1 + micros_str.length() + 2 + pid_str.length() +
              1 + pad + cnt_str.length());
  out += prefix;
  out += '_';
  out += micros_str;
  out += "_p";
  out += pid_str;
  out += '_';
  if (pad > 0) {
    out.append(pad, '0');
  }
  out += cnt_str;

  return out;
}

} // namespace fuzzpilot
