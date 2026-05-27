## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-05-27 - Replace std::ostringstream with std::string::reserve in hot JSON serialization paths
**Learning:** `std::ostringstream` introduces significant overhead (memory allocation, virtual function calls, locale checks) in performance-critical JSON serialization paths like telemetry processing (`afl_stats_json`) and model payload generation.
**Action:** Use pre-allocated `std::string` objects (via `reserve()`) combined with direct string concatenation (`+=` and `std::to_string()`) to serialize JSON formats in hot-loops. This minimizes unnecessary allocations and bypasses the stream buffer synchronization overhead.
