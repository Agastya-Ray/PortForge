#include "pe-console-api.h"

static volatile uint64_t initial_a = UINT64_C(0xfedcba9876543211);
static volatile uint64_t initial_b = UINT64_C(0x800000000000007f);
static volatile int64_t initial_signed = -INT64_C(0x123456789abcdef);
static volatile int64_t signed_divisor = -INT64_C(7919);

void pf_entry(void) {
    uint64_t result[24];
    uint64_t a = initial_a, b = initial_b;
    int64_t signed_value = initial_signed, divisor = signed_divisor;
    for (unsigned i = 0; i < 6; ++i) {
        result[4 * i] = a / b;
        result[4 * i + 1] = a % b;
        result[4 * i + 2] = (uint64_t)(signed_value / divisor);
        result[4 * i + 3] = (uint64_t)(signed_value % divisor);
        a = a * UINT64_C(0x4f1bbcdc) + UINT64_C(0x91273);
        b = (b - UINT64_C(0x1234)) | 1;
        signed_value /= 3;
    }
    uint32_t written;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), result, sizeof(result), &written, 0) || written != sizeof(result)) ExitProcess(85);
    ExitProcess(0);
}
