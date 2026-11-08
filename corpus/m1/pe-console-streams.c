#include "pe-console-api.h"

void pf_entry(void) {
    static const uint8_t binary[] = {0x00, 0xff, 0x0d, 0x0a, 0x00, 0x1a};
    static const char diagnostic[] = "guest diagnostic\r\n";
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    void *err = GetStdHandle(STD_ERROR_HANDLE);
    uint32_t written = 123;
    if (!WriteFile(out, binary, 0, &written, 0)) ExitProcess(74);
    if (written != 0) ExitProcess(75);
    if (!WriteFile(out, binary, sizeof(binary), &written, 0)) ExitProcess(76);
    if (written != sizeof(binary)) ExitProcess(77);
    if (!WriteFile(err, diagnostic, sizeof(diagnostic) - 1, &written, 0)) ExitProcess(78);
    if (written != sizeof(diagnostic) - 1) ExitProcess(79);
    ExitProcess(23);
}
