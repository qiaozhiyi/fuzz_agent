## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.
## 2024-05-28 - Replace std::ostringstream with manual bit shift for hex conversions
**Learning:** Using `std::ostringstream` for formatting numbers to hex strings causes significant performance overhead and unnecessary heap allocations, acting as a bottleneck in hot-loops like hashing strings and byte vectors.
**Action:** For simple string formatting like 64-bit to hex conversions, use pre-allocated `std::string` (`std::string(16, '0')`) combined with bitwise shifting and masking to extract nibbles directly, bypassing standard stream overhead entirely.
