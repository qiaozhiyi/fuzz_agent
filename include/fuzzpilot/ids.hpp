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

  std::ostringstream out;
  out << prefix << "_" << micros << "_p" << current_process_id() << "_" << std::setfill('0')
      << std::setw(4) << counter.fetch_add(1, std::memory_order_relaxed);
  return out.str();
}

}  // namespace fuzzpilot
