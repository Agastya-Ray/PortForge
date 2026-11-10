# ADR 013 Synchronous standard input and Windows error codes

The Wine console runner exposes standard input as a guest token alongside
its existing output and error tokens. A synchronous ReadFile can read from
standard input, while a synchronous WriteFile writes to standard output or
standard error. The import binder checks the complete data buffer and the
four-byte transfer count. ReadFile requires the data buffer to be writable.
The runner requires a null overlapped argument and writes the transfer count
back in little-endian order. Unsupported handle tokens return the Windows
invalid-handle error.

The guest's GetLastError and SetLastError use state owned by the active
Wine binding context. For calls forwarded to Wine, the wrapper installs
the guest error value before calling Wine, captures the resulting value,
and restores the host thread's previous value. The runner currently has
one guest thread per context.

The console corpus feeds the same binary input file to the original Windows
program and the translated version. The new fixture checks a short read,
end of file, the error from an invalid handle, explicit error storage, and
an exact echo of bytes including embedded zeros and mixed line endings. CI
builds it with Clang, MinGW GCC, and MSVC and compares native and ARM64 results.

- [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile)
- [Testing for end of file](https://learn.microsoft.com/en-us/windows/win32/FileIO/testing-for-the-end-of-a-file)
