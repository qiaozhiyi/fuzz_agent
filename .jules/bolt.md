## 2024-05-24 - Avoid std::ostringstream in telemetry hot paths
**Learning:** `std::ostringstream` introduces significant dynamic memory allocation overhead and formatting overhead, especially noticeable when called frequently to serialize telemetry JSON structures in tight loops like mutation events.
**Action:** Always prefer pre-allocating a `std::string` with `reserve()` and standard string concatenation (e.g., `operator+=` and `std::to_string()`) when constructing simple JSON strings.
