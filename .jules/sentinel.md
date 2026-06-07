## 2024-10-25 - Resource Leaks in Temporary File Creation
**Vulnerability:** Missing RAII for file descriptors and temporary files created via `mkstemp()`. If subsequent C++ standard library calls (like `std::filesystem::path` constructor) threw exceptions, the file descriptor and temporary file were leaked.
**Learning:** Mixing POSIX C APIs (which require manual cleanup) with C++ APIs (which can throw exceptions) creates hidden resource leak vectors.
**Prevention:** Always encapsulate POSIX resources (`fd`, `FILE*`, temporary file paths) in RAII wrapper classes immediately after acquisition to ensure secure cleanup during stack unwinding.
