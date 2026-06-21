## 2024-06-21 - [Option Injection in curl]
**Vulnerability:** The `curl` command constructed in `src/model/gateway.cpp` does not use `--` to separate options from positional arguments (like `endpoint_`).
**Learning:** This is an option injection vulnerability. If `endpoint_` is dynamically configured or derived from an untrusted source, it could inject malicious curl flags (e.g., `-o` to overwrite files).
**Prevention:** Always use `--` to separate options from positional arguments when executing command line tools like `curl`.
