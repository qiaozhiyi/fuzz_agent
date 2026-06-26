## 2026-06-26 - Option Injection Vulnerability in CLI execution
**Vulnerability:** Command line tools like `curl` were executed with a dynamic URL parameter (`endpoint_`) without separating positional arguments from options using `--`.
**Learning:** This exposes the application to option injection attacks, where a malicious URL starting with `-` could be parsed as a command-line flag (e.g. `-o` to overwrite files).
**Prevention:** Always use `--` to explicitly mark the end of options before passing untrusted or dynamic strings as positional arguments to CLI executables via `run_process_capture`.
