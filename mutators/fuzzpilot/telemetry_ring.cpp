#include "telemetry_ring.hpp"

#include <cstdlib>
#include <fstream>

void TelemetryRing::hit() {
  ++recipe_hits_;
}

void TelemetryRing::miss() {
  ++recipe_misses_;
}

void TelemetryRing::record(const std::string& op,
                           std::size_t offset,
                           std::size_t size_before,
                           std::size_t size_after,
                           bool recipe_hit) {
  (void)recipe_hit;
  ++mutation_count_;
  ++op_counts_[op];
  last_offset_ = offset;
  last_size_before_ = size_before;
  last_size_after_ = size_after;
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
  for (const auto& [op, count] : op_counts_) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\"" << op << "\":" << count;
  }
  output << "}}\n";
}

