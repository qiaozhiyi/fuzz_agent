#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class TelemetryRing {
 public:
  void hit();
  void miss();
  void record(const std::string& op,
              std::size_t offset,
              std::size_t size_before,
              std::size_t size_after,
              bool recipe_hit);
  void flush_from_environment() const;

 private:
  std::uint64_t recipe_hits_ = 0;
  std::uint64_t recipe_misses_ = 0;
  std::uint64_t mutation_count_ = 0;
  std::map<std::string, std::uint64_t> op_counts_;
  std::size_t last_offset_ = 0;
  std::size_t last_size_before_ = 0;
  std::size_t last_size_after_ = 0;
};

