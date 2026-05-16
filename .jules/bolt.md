## 2024-05-16 - String Allocation Bottlenecks
**Learning:** The codebase had duplicated, unoptimized string utilities scattered across 10 files. `json_escape` used `std::ostringstream` which was slow, and `trim` returned `std::string` causing unnecessary allocations.
**Action:** Centralized these into `fuzzpilot/string_util.hpp`, using `std::string_view` for `trim` and `std::string::reserve` for `json_escape`. When using `trim` results with functions requiring `std::string` (like `std::stoi`), remember to explicitly cast the `std::string_view` back to `std::string`.
