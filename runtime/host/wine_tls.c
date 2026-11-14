/* Wine's ARM64 PE startup does not supply a C TLS directory. The PE loader
 * copies this template and assigns _tls_index for the process and new threads.
 * SoftFloat uses only constant-initialized C scalars, with no TLS callbacks. */
#if defined(_WIN32) && defined(__aarch64__)
#include <windows.h>

#pragma section(".tls", read, write)
#pragma section(".tls$ZZZ", read, write)
#pragma section(".rdata$T", read)
__declspec(allocate(".tls")) __declspec(align(16)) char pf_tls_start = 0;
__declspec(allocate(".tls$ZZZ")) char pf_tls_end = 0;
DWORD _tls_index;
static const PIMAGE_TLS_CALLBACK pf_tls_callbacks[] = {NULL};
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY64 _tls_used = {
    (ULONGLONG)(ULONG_PTR)&pf_tls_start,
    (ULONGLONG)(ULONG_PTR)&pf_tls_end,
    (ULONGLONG)(ULONG_PTR)&_tls_index,
    (ULONGLONG)(ULONG_PTR)pf_tls_callbacks, 0, 0,
};
#endif
