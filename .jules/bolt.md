## 2026-06-16 - Optimize string building with snprintf
**Learning:** Using `std::ostringstream` in frequently called functions like `afl_stats_json` and `afl_stats_summary` introduces unnecessary overhead from dynamic allocations.
**Action:** Replaced `std::ostringstream` with pre-allocated `std::string` and `std::snprintf` (`%g`) for doubles to improve performance in hot paths.
