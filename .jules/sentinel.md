
## 2026-06-25 - Prevent Command Line Option Injection
**Vulnerability:** Command line option injection in curl arguments via an unvalidated endpoint string.
**Learning:** When passing external or dynamically configured strings to command line tools (like curl), treating them directly as positional arguments can allow them to be parsed as options if they start with a hyphen.
**Prevention:** Always use `--` to explicitly separate options from positional arguments when executing command line utilities.
