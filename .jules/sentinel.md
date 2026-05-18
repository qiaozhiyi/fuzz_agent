## 2025-05-24 - API Key Exposure in Command Line Arguments
**Vulnerability:** The API key is passed directly via command line arguments (`-H`, `Authorization: Bearer <key>`) when invoking `curl` via `run_process_capture` in `src/model/gateway.cpp`.
**Learning:** Command line arguments passed to a subprocess are visible to all users on the system via utilities like `ps aux`, leading to potential secret leakage.
**Prevention:** Sensitive information such as API keys and credentials should never be passed as command line arguments. Use environment variables or temporary files with restricted permissions, or standard configuration files to supply secrets to subprocesses securely.
