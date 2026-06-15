## 2024-05-14 - Initialize Bolt Journal
**Learning:** Initializing journal for Bolt to record critical performance learnings.
**Action:** Use this file to record non-obvious codebase-specific insights.
## 2026-06-15 - Optimize AflStats Serialization
**Learning:** Using `std::ostringstream` for frequently called JSON and summary serialization of structs like `AflStats` causes notable overhead due to dynamic allocations.
**Action:** Replace `std::ostringstream` with `std::string::reserve` and direct string concatenation (`+=`) using `std::to_string` and `fuzzpilot::format_double` for performance-critical serialization paths.
