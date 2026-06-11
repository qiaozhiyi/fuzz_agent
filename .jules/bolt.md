## 2025-02-18 - Optimize double formatting in hot paths
**Learning:** Using `std::ostringstream` for formatting doubles in hot paths adds significant memory allocation overhead. Standard string concatenation is roughly 2x faster, but standard `std::to_string(double)` retains trailing zeros.
**Action:** Implement and use a custom utility function `fuzzpilot::format_double()` utilizing `std::snprintf` to format doubles efficiently without trailing zeros, and use pre-allocated `std::string` (`reserve`) with standard concatenation to avoid dynamic allocations.
