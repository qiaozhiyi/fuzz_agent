## 2026-06-03 - std::ostringstream overhead in stable_text_hash
**Learning:** `std::ostringstream` introduces significant overhead for simple string construction and hex serialization in hot paths like hashing.
**Action:** Use pre-allocated `std::string` combined with manual bit-shifting and lookup tables for fast hex serialization instead of `std::ostringstream`.
