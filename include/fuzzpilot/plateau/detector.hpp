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
  // Minimum number of distinct samples required in the window before any
  // plateau decision can be made — guards against single-sample noise.
  std::size_t min_samples = 20;
  // If true, also require monotonic non-growth across all intermediate
  // samples (not just first vs last). Set false to recover legacy behaviour.
  bool require_monotonic = true;
  // If true, agent / controller can force a plateau (kill-switch for
  // experiments). Default false; experiment matrix toggles this off
  // entirely via the no-plateau ablation.
  bool disabled = false;
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
};

std::string plateau_event_json(const PlateauEvent& event);

}  // namespace fuzzpilot

