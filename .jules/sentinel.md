## 2024-05-21 - [Sensitive Resource Cleanup Vulnerability]
**Vulnerability:** Found sensitive temporary files (containing API keys and model payloads) being manually deleted at the end of a block, which could leak files if an exception is thrown or an early return occurs.
**Learning:** Manual resource cleanup for temporary files in C++ bypasses exception safety. If a function throws or exits early, the `std::filesystem::remove` call is skipped, leaving sensitive data on disk.
**Prevention:** Always use RAII (Resource Acquisition Is Initialization) wrappers like `ScopedTempFile` for managing temporary files and other sensitive external resources to ensure automatic, exception-safe cleanup upon scope exit.

## 2024-05-22 - [API Key Exposure in Process Arguments]
**Vulnerability:** Found the Gemini API key being passed as a query parameter in the URL directly in the `curl` command line arguments, exposing it to process listings like `ps aux`.
**Learning:** Passing sensitive data via command line arguments makes them visible system-wide through process inspection tools and logs.
**Prevention:** Always use secure temporary files (e.g., via `ScopedTempFile` and `make_private_tempfile`) to pass secrets to external processes (like `curl` using the `-H @<filepath>` option).
