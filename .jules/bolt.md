
## 2026-06-19 - Optimize std::ostringstream allocations
**Learning:** The use of std::ostringstream inside highly frequent telemetry formatting causes significant allocation overhead. Floating point formatting is particularly costly without dynamic allocations.
**Action:** Used std::string::reserve with sequential += operations and std::snprintf for floating point values to dramatically reduce overhead.
