## 2024-05-21 - [Sensitive Resource Cleanup Vulnerability]
**Vulnerability:** Found sensitive temporary files (containing API keys and model payloads) being manually deleted at the end of a block, which could leak files if an exception is thrown or an early return occurs.
**Learning:** Manual resource cleanup for temporary files in C++ bypasses exception safety. If a function throws or exits early, the `std::filesystem::remove` call is skipped, leaving sensitive data on disk.
**Prevention:** Always use RAII (Resource Acquisition Is Initialization) wrappers like `ScopedTempFile` for managing temporary files and other sensitive external resources to ensure automatic, exception-safe cleanup upon scope exit.

## 2025-01-22 - [Process Listing API Key Leak]
**Vulnerability:** Found an API key being passed directly in a URL via a query parameter to a `curl` command. This exposes the API key in system process listings (e.g., `ps aux`) because command-line arguments (`argv`) are visible to all users on the system.
**Learning:** Passing sensitive information (like API keys) in the command-line arguments of spawned processes is a critical security risk.
**Prevention:** Always write sensitive data to a secure temporary file created with `mkstemp` and use the tool's file-read parameters (e.g., `curl -H @<filepath>`) to pass it.
