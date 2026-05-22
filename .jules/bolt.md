## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-06-25 - Avoid std::ostringstream for simple formatting and hex serialization
**Learning:** `std::ostringstream` causes significant overhead through dynamic heap allocations, especially inside fast code paths like JSON construction (`afl_stats_json`) and hex formatting (`stable_text_hash`, `stable_seed_hash_hex`). Micro-benchmarks confirmed that using `std::ostringstream` for hex dumping takes ~8-10x longer than manual formatting.
**Action:** Replace `std::ostringstream` with pre-allocated `std::string` (`std::string::reserve`) and standard string concatenation or manual bit shifting (for hex strings). This avoids unnecessary dynamic allocations and improves performance inside hot execution contexts.
