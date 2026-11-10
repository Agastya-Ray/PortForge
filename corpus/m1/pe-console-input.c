#include "pe-console-api.h"

void pf_entry(void) {
    void *in = GetStdHandle(STD_INPUT_HANDLE);
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint8_t bytes[16];
    uint32_t read = 123, written = 456;

    SetLastError(0x1234);
    if (GetLastError() != 0x1234) ExitProcess(82);
    if (!ReadFile(in, bytes, sizeof(bytes), &read, 0)) ExitProcess(83);
    if (read != 9) ExitProcess(84);
    if (!WriteFile(out, bytes, read, &written, 0)) ExitProcess(85);
    if (written != read) ExitProcess(86);
    if (!ReadFile(in, bytes, sizeof(bytes), &read, 0)) ExitProcess(87);
    if (read != 0) ExitProcess(88);

    read = 123;
    SetLastError(0x5678);
    if (ReadFile(0, bytes, sizeof(bytes), &read, 0)) ExitProcess(89);
    if (GetLastError() != 6 || read != 0) ExitProcess(90);
    ExitProcess(0);
}
