## 2024-05-24 - Resource Leak in Temp File Creation
**Vulnerability:** The `make_private_tempfile` function relied on manual cleanup of file descriptors and unlinking temporary files on error paths. If an exception occurred (e.g., during `std::filesystem::path` construction), the file descriptor and the file on disk would leak. Since this function is used to create files containing sensitive API keys, a leaked file could potentially be recovered.
**Learning:** The lack of RAII for resource management meant that exceptions could bypass the manual cleanup logic, leading to resource and data leakage.
**Prevention:** Always use RAII wrappers to manage file descriptors and temporary file paths, ensuring cleanup occurs automatically during stack unwinding on exception paths.
