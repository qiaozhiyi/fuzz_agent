## 2024-05-18 - Avoid std::ostringstream in hot code paths
**Learning:** std::ostringstream introduces significant dynamic heap allocation overhead for simple string formatting and hex serialization, which can impact performance in hot code paths like telemetry or string hashing.
**Action:** Do not use `std::ostringstream` for simple string building. Instead, pre-allocate a `std::string` using `reserve()` and standard concatenation (`+=`, `std::to_string`), or manual bit-shifting for hex serialization.
