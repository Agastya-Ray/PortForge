#include "pe-console-api.h"

#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

static volatile uint64_t inputs[] = {
    UINT64_C(0x76543210fedcba98), UINT64_C(0xffff000010203040),
    UINT64_C(0x123456789abcdef0), UINT64_C(0x0101010190909090),
    UINT64_C(0xfedcba9876543210),
};

static NOINLINE uint64_t calculate(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    return ((a + b) ^ (c - d)) + e;
}

void pf_entry(void) {
    uint64_t result[2];
    result[0] = calculate(inputs[0], inputs[1], inputs[2], inputs[3], inputs[4]);
    result[1] = calculate(inputs[4], inputs[3], inputs[2], inputs[1], inputs[0]);
    void *out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written;
    if (!WriteFile(out, result, sizeof(result), &written, 0)) ExitProcess(80);
    if (written != sizeof(result)) ExitProcess(81);
    ExitProcess(0);
}
