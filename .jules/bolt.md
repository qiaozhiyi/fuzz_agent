## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.
## 2024-05-22 - Optimize hex serialization in stable_text_hash
**Learning:** Using `std::ostringstream` for simple hex serialization inside `stable_text_hash` incurs significant allocation overhead, nearly doubling the execution time compared to manual bit-shifting.
**Action:** Avoid `std::ostringstream` for critical path hex serialization. Use pre-allocated `std::string` with `reserve` and manual bit shifting instead.
