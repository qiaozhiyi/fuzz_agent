## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-05-21 - Optimize hex serialization and simple string formatting
**Learning:** `std::ostringstream` carries significant overhead due to memory allocation and stream formatting logic, making it unsuitable for hot-path ID generation and 64-bit hash hex-string serialization.
**Action:** Use pre-allocated `std::string` (`std::string::reserve`) combined with standard string concatenation (`+=`, `std::to_string`) or manual bit shifting for performance-critical string construction to reduce latency by over 60%.
