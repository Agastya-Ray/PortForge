#include "pe-console-api.h"

static volatile uint64_t initial_a = UINT64_C(0xfedcba9876543211);
static volatile uint64_t initial_b = UINT64_C(0x800000000000007f);

void pf_entry(void) {
    uint64_t result[16];
    uint64_t a = initial_a, b = initial_b;
    for (unsigned i = 0; i < 8; ++i) {
        uint64_t product = a * b;
        uint64_t low = product + a;
        uint64_t carry = low < product;
        b = ~b + carry;
        a = UINT64_C(0) - low;
        result[2 * i] = a;
        result[2 * i + 1] = b;
    }
    uint32_t written;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), result, sizeof(result), &written, 0)) ExitProcess(81);
    if (written != sizeof(result)) ExitProcess(82);
    ExitProcess(0);
}
