## YYYY-MM-DD - [Fix curl option injection vulnerability]
**Vulnerability:** Command-line tool (curl) option injection via dynamically configured endpoint without `--` separator.
**Learning:** External variables passed to command line utilities can be interpreted as flags if not separated by `--`.
**Prevention:** Always use `--` to separate positional arguments from options when using command line tools.
