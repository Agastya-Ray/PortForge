#include "pe-console-api.h"

static volatile uint32_t initial = UINT32_C(0x531c7ab2);

void pf_entry(void) {
    uint32_t result[8];
    uint32_t value = initial;
    for (unsigned i = 0; i < sizeof(result) / sizeof(result[0]); ++i) {
        value = (value + UINT32_C(0x92c7a1e5)) ^ UINT32_C(0x4107332f);
        if (value > UINT32_C(0xc0000000)) value ^= UINT32_C(0x7010f0aa);
        result[i] = value;
    }
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, result, sizeof(result), &written, 0)) ExitProcess(72);
    if (written != sizeof(result)) ExitProcess(73);
    ExitProcess(0);
}
