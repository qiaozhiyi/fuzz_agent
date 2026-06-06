## 2024-05-14 - Replace std::ostringstream with std::string::reserve in JSON escaping
**Learning:** `std::ostringstream` has significant performance overhead for simple string construction, especially in hot paths like telemetry JSON serialization.
**Action:** Use `std::string::reserve` combined with manual string appending instead of `std::ostringstream` for JSON serialization to reduce allocation overhead.
