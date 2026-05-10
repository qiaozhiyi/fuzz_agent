#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace fuzzpilot {

inline std::string make_id(const std::string& prefix) {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  std::ostringstream out;
  out << prefix << "_" << millis << "_" << std::setfill('0') << std::setw(4)
      << counter.fetch_add(1, std::memory_order_relaxed);
  return out.str();
}

}  // namespace fuzzpilot

