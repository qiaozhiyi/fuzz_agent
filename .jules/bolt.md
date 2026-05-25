## 2024-05-21 - Optimize std::string memory allocations in config/telemetry string splits
**Learning:** `std::string::substr` creates a heap allocation and deep copy, which is highly wasteful when immediately passing the result to `trim` inside hot-loop telemetry/config parsers.
**Action:** Use `std::string_view` explicitly (`std::string_view(str).substr(...)`) when extracting substrings before trimming to bypass temporary heap allocations, preserving performance during critical operations.

## 2024-11-20 - Optimize `std::ostringstream` overhead in `make_id`
**Learning:** `std::ostringstream` stream formatting adds significant overhead for simple string concatenation, especially in frequently called hot paths like ID generation.
**Action:** Replace `std::ostringstream` with manual `std::string` concatenation using `std::string::reserve`, `std::to_string`, and direct string append operations to minimize memory allocations and improve speed.
