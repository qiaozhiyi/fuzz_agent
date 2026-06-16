## 2025-02-14 - Harden curl invocation and remove insecure temp file code
**Vulnerability:** Dead code with a TOCTOU vulnerability (`write_private_text_file`) and potential curl argument injection via configured endpoint.
**Learning:** Even unused code presents a risk if left in the codebase. Command line tools like `curl` are susceptible to option injection if inputs starting with `-` are not separated from arguments by `--`.
**Prevention:** Always remove unused insecure code. Always use `--` to signify the end of options when executing command line tools with dynamically configured arguments.
