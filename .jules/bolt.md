## YYYY-MM-DD - [Optimize Telemetry Hot Paths]
**Learning:** `std::ostringstream` introduces significant dynamic allocation overhead in hot paths for formatting telemetry outputs.
**Action:** Use pre-allocated `std::string` with `.reserve()` and `std::snprintf` (for doubles, dropping trailing zeros via `%g`) to reduce memory allocations and improve overall performance in string formatting paths.
