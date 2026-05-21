#include "telemetry_ring.hpp"

#include <cstdlib>
#include <fstream>

namespace {

// Stable, human-readable name for each enum value. Kept in lock-step
// with `CompactMutationOp` in recipe_cache.hpp. If a new op is added
// there, add the matching string here and bump kNumOps in
// telemetry_ring.hpp if needed.
const char* op_name_for(std::size_t index) {
  switch (static_cast<CompactMutationOp>(index)) {
    case CompactMutationOp::BitFlip:             return "bit_flip";
    case CompactMutationOp::OverwriteRange:      return "overwrite_range";
    case CompactMutationOp::InsertToken:         return "insert_token";
    case CompactMutationOp::Arith:               return "arith";
    case CompactMutationOp::Splice:              return "splice";
    case CompactMutationOp::DeleteBlock:         return "delete_block";
    case CompactMutationOp::DictionaryOverwrite: return "dictionary_overwrite";
  }
  return nullptr;  // out-of-range / future op — caller will tally to op_other_
}

}  // namespace

void TelemetryRing::hit() {
  ++recipe_hits_;
}

void TelemetryRing::miss() {
  ++recipe_misses_;
}

void TelemetryRing::record(CompactMutationOp op,
                           std::size_t offset,
                           std::size_t size_before,
                           std::size_t size_after,
                           bool recipe_hit) {
  (void)recipe_hit;
  ++mutation_count_;
  const auto idx = static_cast<std::size_t>(op);
  if (idx < kNumOps) {
    ++op_counts_[idx];
  } else {
    ++op_other_;
  }
  last_offset_ = offset;
  last_size_before_ = size_before;
  last_size_after_ = size_after;
}

void TelemetryRing::record(const std::string& op_name,
                           std::size_t offset,
                           std::size_t size_before,
                           std::size_t size_after,
                           bool recipe_hit) {
  // Small if-chain rather than a std::map lookup. With 7 named ops the
  // chain finishes in 1-2 compares for the common cases (bit_flip and
  // overwrite_range), which empirically dominate.
  CompactMutationOp op = CompactMutationOp::BitFlip;
  bool matched = true;
  if      (op_name == "bit_flip")             op = CompactMutationOp::BitFlip;
  else if (op_name == "overwrite_range")      op = CompactMutationOp::OverwriteRange;
  else if (op_name == "insert_token")         op = CompactMutationOp::InsertToken;
  else if (op_name == "arith")                op = CompactMutationOp::Arith;
  else if (op_name == "splice")               op = CompactMutationOp::Splice;
  else if (op_name == "delete_block")         op = CompactMutationOp::DeleteBlock;
  else if (op_name == "dictionary_overwrite") op = CompactMutationOp::DictionaryOverwrite;
  else                                        matched = false;

  if (matched) {
    record(op, offset, size_before, size_after, recipe_hit);
  } else {
    // Unknown op — still update the counters but route into the
    // catch-all bucket so telemetry consumers can see it.
    ++mutation_count_;
    ++op_other_;
    last_offset_ = offset;
    last_size_before_ = size_before;
    last_size_after_ = size_after;
  }
}

void TelemetryRing::flush_from_environment() const {
  const char* path = std::getenv("FUZZPILOT_MUTATOR_TELEMETRY");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream output(path, std::ios::app);
  if (!output) {
    return;
  }
  output << "{";
  output << "\"recipe_hits\":" << recipe_hits_ << ",";
  output << "\"recipe_misses\":" << recipe_misses_ << ",";
  output << "\"mutation_count\":" << mutation_count_ << ",";
  output << "\"last_offset\":" << last_offset_ << ",";
  output << "\"last_size_before\":" << last_size_before_ << ",";
  output << "\"last_size_after\":" << last_size_after_ << ",";
  output << "\"operators\":{";
  bool first = true;
  for (std::size_t i = 0; i < kNumOps; ++i) {
    if (op_counts_[i] == 0) continue;
    const char* name = op_name_for(i);
    if (name == nullptr) continue;
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\"" << name << "\":" << op_counts_[i];
  }
  if (op_other_ > 0) {
    if (!first) {
      output << ",";
    }
    output << "\"other\":" << op_other_;
  }
  output << "}}\n";
}
