## 2024-05-28 - Avoid Exposing Secrets in Command Line Arguments
**Vulnerability:** The OpenAI model API key was being passed directly to `curl` via the `argv` array (`-H "Authorization: Bearer <key>"`), exposing the secret in the system's process listing (e.g., via `ps aux`).
**Learning:** Secrets should never be passed as command line arguments to subprocesses.
**Prevention:** Always write sensitive data to temporary files (with restrictive permissions, securely generated, e.g., via `mkstemp`) and instruct subprocesses to read them from these files instead, ensuring proper cleanup afterward.
