## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.
## 2024-11-20 - Optimize string construction by replacing std::ostringstream
**Learning:** `std::ostringstream` introduces significant overhead due to dynamic heap allocations and stream formatting machinery, which is particularly detrimental in hot code paths like ID generation, hashing, and JSON payload construction.
**Action:** Use pre-allocated `std::string` (`std::string::reserve`) combined with standard string concatenation (`+=`, `std::to_string`) or manual bit shifting (for hex serialization) to avoid dynamic allocations and improve performance in critical operations.
