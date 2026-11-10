#include "pe-console-api.h"

void pf_entry(void) {
    static const char message[] = "performance counter ok\n";
    int64_t frequency, repeated_frequency, first, second;
    if (!QueryPerformanceFrequency(&frequency) || frequency <= 0) ExitProcess(91);
    if (!QueryPerformanceCounter(&first)) ExitProcess(92);
    if (!QueryPerformanceCounter(&second) || second < first) ExitProcess(93);
    if (!QueryPerformanceFrequency(&repeated_frequency) || repeated_frequency != frequency) ExitProcess(94);
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, message, sizeof(message) - 1, &written, 0)) ExitProcess(95);
    if (written != sizeof(message) - 1) ExitProcess(96);
    ExitProcess(0);
}
