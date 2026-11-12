#include "pe-console-api.h"

void pf_entry(void) {
    static const char message[] = "system calls ok\n";
    uint32_t process = GetCurrentProcessId();
    uint32_t thread = GetCurrentThreadId();
    if (process == 0 || GetCurrentProcessId() != process) ExitProcess(97);
    if (thread == 0 || GetCurrentThreadId() != thread) ExitProcess(98);
    uint64_t time;
    GetSystemTimeAsFileTime(&time);
    if (time == 0) ExitProcess(99);
    uint64_t first = GetTickCount64();
    Sleep(1);
    if (GetTickCount64() < first) ExitProcess(100);
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, message, sizeof(message) - 1, &written, 0)) ExitProcess(101);
    if (written != sizeof(message) - 1) ExitProcess(102);
    ExitProcess(0);
}
