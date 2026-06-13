## 2026-06-13 - std::ostringstream Overhead in Hot Paths
**Learning:** `std::ostringstream` incurs significant dynamic allocation overhead in this codebase's hot paths (e.g., gateway payload formatting). Additionally, standard `std::to_string(double)` retains trailing zeros which can increase payload size unnecessarily.
**Action:** Prefer pre-allocated `std::string` with `reserve()` and direct sequential `+=` concatenation. Use the centralized `fuzzpilot::format_double()` for floating point formatting to avoid trailing zeros without ostringstream overhead.
