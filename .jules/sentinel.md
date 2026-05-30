## 2024-05-21 - [Sensitive Resource Cleanup Vulnerability]
**Vulnerability:** Found sensitive temporary files (containing API keys and model payloads) being manually deleted at the end of a block, which could leak files if an exception is thrown or an early return occurs.
**Learning:** Manual resource cleanup for temporary files in C++ bypasses exception safety. If a function throws or exits early, the `std::filesystem::remove` call is skipped, leaving sensitive data on disk.
**Prevention:** Always use RAII (Resource Acquisition Is Initialization) wrappers like `ScopedTempFile` for managing temporary files and other sensitive external resources to ensure automatic, exception-safe cleanup upon scope exit.

## 2024-05-24 - [Command-Line Secret Exposure]
**Vulnerability:** API keys passed as command-line arguments (e.g., query parameters to curl) are exposed to other processes via process listings like `ps aux` or `/proc/[pid]/cmdline`.
**Learning:** Embedding secrets directly in command-line arguments is globally visible and insecure on many systems.
**Prevention:** Always write sensitive data to a secure temporary file created with `mkstemp` and 0600 permissions, and use the tool's file-read parameters (e.g., `curl -H @<filepath>`) to read them securely.
