#include "pe-console-api.h"

void pf_entry(void) {
    static const char message[] = "PortForge console\r\nsecond line\n";
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, message, sizeof(message) - 1, &written, 0)) ExitProcess(70);
    if (written != sizeof(message) - 1) ExitProcess(71);
    ExitProcess(0);
}
