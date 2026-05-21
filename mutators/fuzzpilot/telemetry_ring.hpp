#pragma once

#include "recipe_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

class TelemetryRing {
 public:
  // One bucket per `CompactMutationOp`. The enum currently has 7 named
  // members; we size the array generously so future operator additions
  // don't require a header re-fanout.
  static constexpr std::size_t kNumOps = 16;

  void hit();
  void miss();

  // Fast-path record. The enum index translates directly to an array
  // slot — no hashing, no allocation. AFL++ calls the mutator hot path
  // millions of times per minute; the previous `std::map<string, u64>`
  // implementation showed up at ~2-5% of total throughput in profiles.
  void record(CompactMutationOp op,
              std::size_t offset,
              std::size_t size_before,
              std::size_t size_after,
              bool recipe_hit);

  // String overload, kept for tests and any caller that doesn't have a
  // typed op handy. Performs a small O(name-list) compare; unknown
  // names fall into the `op_other_` bucket so we never lose a sample.
  void record(const std::string& op_name,
              std::size_t offset,
              std::size_t size_before,
              std::size_t size_after,
              bool recipe_hit);

  void flush_from_environment() const;

 private:
  std::uint64_t recipe_hits_ = 0;
  std::uint64_t recipe_misses_ = 0;
  std::uint64_t mutation_count_ = 0;
  // Fixed-size operator counter array indexed by the underlying enum
  // value. Accessing op_counts_[static_cast<size_t>(op)] is one cache
  // line, no hashing, no branching.
  std::uint64_t op_counts_[kNumOps] = {0};
  // Unknown / future operator names go here so the slow path is still
  // observable in telemetry output.
  std::uint64_t op_other_ = 0;
  std::size_t last_offset_ = 0;
  std::size_t last_size_before_ = 0;
  std::size_t last_size_after_ = 0;
};
