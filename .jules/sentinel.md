## 2026-06-20 - Option Injection in curl and Vulnerable Dead Code
**Vulnerability:** Option injection vulnerability by missing `--` before dynamical endpoint in `curl` execution. Leftover dead code with TOCTOU vulnerability (`write_private_text_file`).
**Learning:** External variables passed as arguments to command-line tools can introduce option injection vulnerabilities if not properly separated. Unused functions that contain vulnerabilities increase attack surface.
**Prevention:** Always use `--` to separate dynamically configured arguments (like endpoints) from options in command-line executions. Proactively remove dead code if it has security risks.
