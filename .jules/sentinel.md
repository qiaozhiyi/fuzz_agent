## YYYY-MM-DD - Prevent command option injection in curl
**Vulnerability:** Option injection via dynamically configured endpoint in curl command.
**Learning:** Dynamically configured arguments in command line tools like `curl` can be interpreted as options if they start with a hyphen.
**Prevention:** Always use `--` to separate options from positional arguments when executing command line tools.
