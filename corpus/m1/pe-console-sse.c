#include "pe-console-api.h"

struct inputs {
    uint32_t reset, daz, negative_zero, positive_zero, quiet_nan, one;
    uint64_t positive_denormal, negative_zero_double, negative_denormal, positive_zero_double;
};

void pf_sse_sample(unsigned char *output, const struct inputs *values);

void pf_entry(void) {
    static const struct inputs values = {
        0x1f80, 0x1fc0, 0x80000000, 0, 0x7fc00001, 0x3f800000,
        UINT64_C(1), UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000001), 0,
    };
    unsigned char output[32];
    uint32_t written;
    pf_sse_sample(output, &values);
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output, sizeof(output), &written, 0)
        || written != sizeof(output)) ExitProcess(89);
    ExitProcess(0);
}
