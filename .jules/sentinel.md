## 2024-05-18 - [Resource Leak Mitigation]
**Vulnerability:** File descriptors and temporary files created by `mkstemp` and `open` could leak if an exception was thrown or an early return occurred before explicit cleanup in `src/model/gateway.cpp`.
**Learning:** Manual resource management (`close`, `unlink`) in C++ is error-prone, especially in functions with multiple exit paths or potential exceptions.
**Prevention:** Always use RAII (Resource Acquisition Is Initialization) patterns, such as custom wrapper classes (e.g., `ScopedFileDescriptor` and `ScopedUnlink`), to manage external resources like file descriptors securely.
