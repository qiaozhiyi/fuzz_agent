## YYYY-MM-DD - Optimize String formatting and construction
**Learning:** Using `std::ostringstream` for simple formatting and concatenation causes significant overhead from dynamic allocations in hot paths.
**Action:** Do not use `std::ostringstream` for simple formatting. Use pre-allocated `std::string` (`reserve`) and standard concatenation. Use `std::snprintf` with a stack buffer for floats instead of `std::ostringstream` or `std::to_string` to avoid trailing zeros and dynamic allocation.
