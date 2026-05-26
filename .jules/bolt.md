## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2026-05-26 - Optimize hex string generation for hashes
**Learning:** `std::ostringstream` is surprisingly expensive for simple hex formatting due to locale checks and dynamic allocations.
**Action:** Use pre-allocated `std::string` and manual bit shifting when generating fixed-width hex strings (e.g., hashes) to avoid unnecessary overhead in frequently called functions.
