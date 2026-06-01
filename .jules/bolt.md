## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-05-24 - Optimize `make_id` string formatting
**Learning:** Using `std::ostringstream` for simple, frequent string concatenation (like ID generation) introduces significant overhead compared to pre-allocated `std::string` with `reserve` and `std::to_string`.
**Action:** Avoid `std::ostringstream` for basic string formatting in hot paths; instead use `std::string::reserve` and `+=` to eliminate redundant memory allocations and reduce execution time.
