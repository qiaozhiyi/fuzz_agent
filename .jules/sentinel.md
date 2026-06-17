## 2026-06-17 - Fix TOCTOU vulnerability in dead code
**Vulnerability:** A dead/unused function `write_private_text_file` was present that created files using `open` with `O_CREAT | O_TRUNC` without `O_EXCL`, exposing a symlink/TOCTOU risk.
**Learning:** Unused code carrying security flaws unnecessarily increases the attack surface.
**Prevention:** Proactively remove `[[maybe_unused]]` functions or dead code, especially if they handle files or permissions insecurely.
