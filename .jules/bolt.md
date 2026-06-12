## 2026-06-12 - std::ostringstream overhead in string construction
**Learning:** Using `std::ostringstream` for JSON construction and simple string formatting has a measurable overhead due to dynamic memory allocations and virtual function calls.
**Action:** Avoid `std::ostringstream` for hot paths or frequent string building operations (like `afl_stats_json`). Use `std::string` with `.reserve()` and `+`/`+=` operations, combined with `std::to_string` and `std::to_chars` for primitive types instead.
