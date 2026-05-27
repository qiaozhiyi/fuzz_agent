#include "fuzzpilot/plateau/detector.hpp"

#include "fuzzpilot/ids.hpp"

#include <algorithm>
#include <sstream>

namespace fuzzpilot {

PlateauDetector::PlateauDetector(PlateauConfig config) : config_(config) {}

std::optional<PlateauEvent> PlateauDetector::add_sample(const AflStats& stats,
                                                        const std::string& run_id,
                                                        const std::string& campaign_id) {
  if (config_.disabled) {
    return std::nullopt;
  }

  window_.push_back(stats);
  while (window_.size() > 1 &&
         static_cast<uint64_t>(stats.sampled_at - window_.front().sampled_at) > config_.window_sec) {
    window_.pop_front();
  }

  if (emitted_) {
    return std::nullopt;
  }
  // Guard against single-sample noise: require both a minimum sample count
  // AND a minimum elapsed wall-clock window.
  if (window_.size() < config_.min_samples) {
    return std::nullopt;
  }

  const auto& oldest = window_.front();
  const auto& newest = window_.back();
  const auto observed_window = static_cast<uint64_t>(
      std::max<std::time_t>(0, newest.sampled_at - oldest.sampled_at));
  if (observed_window < config_.window_sec) {
    return std::nullopt;
  }

  // Drop stale samples — if AFL stopped writing fuzzer_stats, do not infer
  // plateau from frozen data.
  if (newest.stale) {
    return std::nullopt;
  }

  const uint64_t execs_delta =
      newest.execs_done >= oldest.execs_done ? newest.execs_done - oldest.execs_done : 0;
  const uint64_t paths_delta =
      newest.paths_total >= oldest.paths_total ? newest.paths_total - oldest.paths_total : 0;
  const uint64_t edges_delta =
      newest.edges_found >= oldest.edges_found ? newest.edges_found - oldest.edges_found : 0;
  const uint64_t crashes_delta =
      newest.unique_crashes >= oldest.unique_crashes ? newest.unique_crashes - oldest.unique_crashes : 0;

  // Multi-point check: walk intermediate samples and require that no
  // single mid-window jump exceeds `max_new_paths`. This catches the case
  // where the start and end happen to match but the middle saw growth and
  // a regression — which we should NOT treat as a plateau.
  if (config_.require_monotonic && window_.size() >= 3) {
    uint64_t worst_paths_jump = 0;
    uint64_t worst_edges_jump = 0;
    for (std::size_t i = 1; i < window_.size(); ++i) {
      const auto& prev = window_[i - 1];
      const auto& curr = window_[i];
      if (curr.paths_total > prev.paths_total) {
        worst_paths_jump = std::max(worst_paths_jump, curr.paths_total - prev.paths_total);
      }
      if (curr.edges_found > prev.edges_found) {
        worst_edges_jump = std::max(worst_edges_jump, curr.edges_found - prev.edges_found);
      }
    }
    if (worst_paths_jump > config_.max_new_paths) {
      return std::nullopt;
    }
    // Edge-aware intermediate jump cap with floor + ceiling. Old "/100" gave
    // 0 for small edge counts (every jump rejected) and unbounded caps for
    // large ones; bounds keep the gate meaningful across run lengths.
    if (oldest.edges_found > 0) {
      const uint64_t pct_cap = std::max<uint64_t>(1, oldest.edges_found / 100);
      const uint64_t cap = std::min(
          config_.monotonic_jump_ceiling,
          std::max(config_.monotonic_jump_floor, pct_cap));
      if (worst_edges_jump > cap) {
        return std::nullopt;
      }
    }
  }

  const auto now = static_cast<uint64_t>(newest.sampled_at);
  const bool stale_last_path =
      newest.last_path == 0 || (now >= newest.last_path && now - newest.last_path >= config_.window_sec);
  const bool execs_ok = execs_delta >= config_.min_execs_delta &&
                        newest.execs_per_sec >= config_.min_execs_per_sec;
  // Edges-aware no_growth: if paths_total is reported (legacy AFL or
  // corpus_count fallback), require ≤max_new_paths AND bounded edges growth.
  // If paths_total is unavailable, fall back to edges-only gate.
  const uint64_t edge_cap =
      std::max(config_.edges_growth_floor,
               oldest.edges_found * config_.edges_growth_pct / 100);
  const bool edges_flat = edges_delta <= edge_cap;
  const bool paths_flat = paths_delta <= config_.max_new_paths;
  const bool no_growth = paths_flat && edges_flat && crashes_delta == 0;

  if (execs_ok && no_growth && stale_last_path) {
    emitted_ = true;
    last_emit_ts_ = now;
    PlateauEvent event;
    event.id = make_id("plateau");
    event.run_id = run_id;
    event.campaign_id = campaign_id;
    event.detected_ts = now;
    event.window_sec = config_.window_sec;
    event.execs_delta = execs_delta;
    event.new_paths_delta = paths_delta;
    event.new_edges_delta = edges_delta;
    event.new_crashes_delta = crashes_delta;
    event.sample_count = window_.size();
    event.reason = paths_delta == 0 && edges_delta == 0 ? "no_new_coverage" :
                   paths_delta == 0 ? "no_new_paths_only" : "low_new_coverage";
    return event;
  }

  return std::nullopt;
}

void PlateauDetector::reset() {
  window_.clear();
  emitted_ = false;
  last_emit_ts_ = 0;
}

std::string plateau_event_json(const PlateauEvent& event) {
  std::ostringstream out;
  out << "{";
  out << "\"plateau_id\":\"" << event.id << "\",";
  out << "\"run_id\":\"" << event.run_id << "\",";
  out << "\"campaign_id\":\"" << event.campaign_id << "\",";
  out << "\"detected_ts\":" << event.detected_ts << ",";
  out << "\"window_sec\":" << event.window_sec << ",";
  out << "\"execs_delta\":" << event.execs_delta << ",";
  out << "\"new_paths_delta\":" << event.new_paths_delta << ",";
  out << "\"new_edges_delta\":" << event.new_edges_delta << ",";
  out << "\"new_crashes_delta\":" << event.new_crashes_delta << ",";
  out << "\"sample_count\":" << event.sample_count << ",";
  out << "\"reason\":\"" << event.reason << "\"";
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot

