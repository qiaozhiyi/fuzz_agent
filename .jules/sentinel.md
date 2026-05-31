## 2024-05-21 - [Sensitive Resource Cleanup Vulnerability]
**Vulnerability:** Found sensitive temporary files (containing API keys and model payloads) being manually deleted at the end of a block, which could leak files if an exception is thrown or an early return occurs.
**Learning:** Manual resource cleanup for temporary files in C++ bypasses exception safety. If a function throws or exits early, the `std::filesystem::remove` call is skipped, leaving sensitive data on disk.
**Prevention:** Always use RAII (Resource Acquisition Is Initialization) wrappers like `ScopedTempFile` for managing temporary files and other sensitive external resources to ensure automatic, exception-safe cleanup upon scope exit.

## 2024-06-15 - [API Key Exposure in Process List]
**Vulnerability:** The GeminiGateway passed the API key as a URL query parameter (`?key=...`), exposing it in the process table when executing the `curl` command (which appears in commands like `ps aux`).
**Learning:** Command-line arguments in `run_process_capture` are visible to all users on the system via the process list.
**Prevention:** Avoid embedding secrets in the `argv` array. Instead, write them to a secure temporary file created with `mkstemp` and use the tool's file-read parameters (e.g., `curl -H @<filepath>`) to prevent exposure in process listings.
