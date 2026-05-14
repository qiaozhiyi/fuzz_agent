## 2026-05-14 - Prevent Command-Line Secret Exposure in curl
**Vulnerability:** API key was passed as a command line argument to `curl` (via `-H`), exposing it to any user on the system running `ps aux`.
**Learning:** `run_process_capture` will spawn a process with the provided `argv`. Any secrets in `argv` are visible system-wide.
**Prevention:** Use secure temporary files (`mkstemp` with `0600` permissions) to store sensitive data and instruct the external tool (e.g., `curl` with `-H @filename`) to read from the file. Ensure cleanup of these temporary files.
