#include "fuzzpilot/plateau/detector.hpp"

#include "fuzzpilot/ids.hpp"

#include <algorithm>
#include <sstream>

namespace fuzzpilot {

PlateauDetector::PlateauDetector(PlateauConfig config) : config_(config) {}

std::optional<PlateauEvent> PlateauDetector::add_sample(const AflStats& stats,
                                                        const std::string& run_id,
                                                        const std::string& campaign_id) {
  window_.push_back(stats);
  while (window_.size() > 1 &&
         static_cast<uint64_t>(stats.sampled_at - window_.front().sampled_at) > config_.window_sec) {
    window_.pop_front();
  }

  if (emitted_ || window_.size() < 2) {
    return std::nullopt;
  }

  const auto& oldest = window_.front();
  const auto& newest = window_.back();
  const auto observed_window = static_cast<uint64_t>(
      std::max<std::time_t>(0, newest.sampled_at - oldest.sampled_at));
  if (observed_window < config_.window_sec) {
    return std::nullopt;
  }

  const uint64_t execs_delta =
      newest.execs_done >= oldest.execs_done ? newest.execs_done - oldest.execs_done : 0;
  const uint64_t paths_delta =
      newest.paths_total >= oldest.paths_total ? newest.paths_total - oldest.paths_total : 0;
  const uint64_t crashes_delta =
      newest.unique_crashes >= oldest.unique_crashes ? newest.unique_crashes - oldest.unique_crashes : 0;

  const auto now = static_cast<uint64_t>(newest.sampled_at);
  const bool stale_last_path =
      newest.last_path == 0 || (now >= newest.last_path && now - newest.last_path >= config_.window_sec);
  const bool execs_ok = execs_delta >= config_.min_execs_delta &&
                        newest.execs_per_sec >= config_.min_execs_per_sec;
  const bool no_growth = paths_delta <= config_.max_new_paths && crashes_delta == 0;

  if (execs_ok && no_growth && stale_last_path) {
    emitted_ = true;
    PlateauEvent event;
    event.id = make_id("plateau");
    event.run_id = run_id;
    event.campaign_id = campaign_id;
    event.detected_ts = now;
    event.window_sec = config_.window_sec;
    event.execs_delta = execs_delta;
    event.new_paths_delta = paths_delta;
    event.new_crashes_delta = crashes_delta;
    event.reason = paths_delta == 0 ? "no_new_paths" : "low_new_paths";
    return event;
  }

  return std::nullopt;
}

void PlateauDetector::reset() {
  window_.clear();
  emitted_ = false;
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
  out << "\"new_crashes_delta\":" << event.new_crashes_delta << ",";
  out << "\"reason\":\"" << event.reason << "\"";
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot

