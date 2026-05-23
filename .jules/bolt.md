## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-05-24 - Optimize ID Generation String Formatting
**Learning:** Using `std::ostringstream` for simple string formatting in frequently called functions like `make_id` introduces unnecessary overhead due to dynamic memory allocations and virtual dispatch.
**Action:** Avoid `std::ostringstream` for simple ID generation or string concatenation on hot paths. Instead, pre-allocate `std::string` using `reserve` and combine with standard string concatenation (`+=`) and `std::to_string()` to minimize allocations and formatting cost.
