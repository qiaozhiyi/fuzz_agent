## 2024-05-15 - [API Key Exposure via Command Line Arguments]
**Vulnerability:** API keys and sensitive headers passed directly to subprocesses (e.g., `curl` via `run_process_capture`) in the `argv` array are exposed to system process listings (`ps aux`), creating an information disclosure risk.
**Learning:** This codebase uses a custom subprocess execution framework (`run_process_capture`) which directly reflects the provided `argv` array to the OS. When passing secrets, it's essential to avoid `argv`.
**Prevention:** Use securely created temporary files with `mkstemp` to pass secrets via file-read parameters (e.g., `curl -H @<filepath>`) and ensure the files are deleted immediately after use.
