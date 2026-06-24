## 2024-06-24 - Avoid std::ostringstream for simple JSON string concatenation
**Learning:** `std::ostringstream` involves dynamic memory allocation and stream formatting overhead, which is detrimental to performance in hot paths like generating JSON telemtry.
**Action:** Use pre-allocated `std::string` with `.reserve()` and standard string concatenation or `std::to_string` instead, as `std::snprintf` may be tricky with variable-length strings.
