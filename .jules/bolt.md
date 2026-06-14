## 2026-06-14 - String Allocation in Hot Paths
**Learning:** Found `std::ostringstream` being used for simple JSON payload construction in `src/model/gateway.cpp`. This causes unnecessary dynamic allocations in a very hot path.
**Action:** Replaced `std::ostringstream` with a pre-allocated `std::string` using `reserve` and sequential `+=` appending, along with a newly introduced `format_double` utility function for efficient string conversion of doubles.
