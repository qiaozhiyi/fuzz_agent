## 2024-05-12 - std::ostringstream performance overhead
**Learning:** `json_escape` implementation currently uses `std::ostringstream` for string building, which is copied 9 times throughout the `src/` directory. Benchmarks show replacing `std::ostringstream` with a reserved `std::string` and `.push_back()`/`.append()` improves performance by roughly 2.8x.
**Action:** Centralize the duplicated `json_escape` function into a `fuzzpilot/json_util.hpp` utility. Optimize its performance by using `std::string` with `.reserve()` instead of `std::ostringstream`.
