#include "pe-console-api.h"

static volatile uint8_t bytes[8] = {0, 1, 0x7f, 0x80, 0xfe, 0xff, 0x0a, 0x0d};
static volatile uint16_t words[8] = {0, 1, 0x7fff, 0x8000, 0xfffe, 0xffff, 0x0a0d, 0x1234};

void pf_entry(void) {
    for (unsigned i = 0; i < 8; ++i) {
        bytes[i] += (uint8_t)(i + 3);
        bytes[i] ^= 0xa5;
        words[i] -= (uint16_t)(0x1234 + i);
        words[i] = (uint16_t)~words[i];
    }
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, (const void *)bytes, sizeof(bytes), &written, 0) || written != sizeof(bytes)) ExitProcess(83);
    if (!WriteFile(out, (const void *)words, sizeof(words), &written, 0) || written != sizeof(words)) ExitProcess(84);
    ExitProcess(0);
}
