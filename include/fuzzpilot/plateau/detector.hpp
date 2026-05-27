#pragma once

#include "fuzzpilot/telemetry/afl_stats.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace fuzzpilot {

struct PlateauConfig {
  uint64_t window_sec = 600;
  uint64_t max_new_paths = 0;
  uint64_t min_execs_delta = 1000;
  double min_execs_per_sec = 1.0;
  std::size_t min_samples = 20;
  bool require_monotonic = true;
  bool disabled = false;
  // Edge-aware plateau: when paths_total is 0 (new AFL++ removed the field),
  // fall back to edges_found growth. Edge growth ≤ max(edges_growth_floor,
  // oldest_edges * edges_growth_pct / 100) within the window counts as plateau.
  uint64_t edges_growth_pct = 1;
  uint64_t edges_growth_floor = 3;
  // Re-emit cooldown: after emitting a plateau, ignore samples for this many
  // seconds then internally reset so the agent can be triggered repeatedly
  // across a long run.
  uint64_t reemit_cooldown_sec = 1200;
  // Monotonic jump tolerance bounds — old hard-coded "/100" produced 0 for
  // small edge counts and over-restrictive caps for large ones.
  uint64_t monotonic_jump_floor = 5;
  uint64_t monotonic_jump_ceiling = 50;
};

struct PlateauEvent {
  std::string id;
  std::string run_id;
  std::string campaign_id;
  uint64_t detected_ts = 0;
  uint64_t window_sec = 0;
  uint64_t execs_delta = 0;
  uint64_t new_paths_delta = 0;
  uint64_t new_edges_delta = 0;
  uint64_t new_crashes_delta = 0;
  std::size_t sample_count = 0;
  std::string reason;
};

class PlateauDetector {
 public:
  explicit PlateauDetector(PlateauConfig config);

  std::optional<PlateauEvent> add_sample(const AflStats& stats,
                                         const std::string& run_id,
                                         const std::string& campaign_id);
  void reset();

 private:
  PlateauConfig config_;
  std::deque<AflStats> window_;
  bool emitted_ = false;
  uint64_t last_emit_ts_ = 0;
};

std::string plateau_event_json(const PlateauEvent& event);

}  // namespace fuzzpilot

