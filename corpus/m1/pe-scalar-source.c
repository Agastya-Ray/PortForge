#include <stdint.h>

/* Compile on x86-64 with the Windows register and stack convention. */
__attribute__((ms_abi))
uint64_t pf_fixture(uint64_t input, uint64_t selector, uint64_t unused1, uint64_t unused2) {
    (void)unused1;
    (void)unused2;
    volatile uint64_t value = input;
    volatile uint32_t low = (uint32_t)selector;
    if (selector == 17) value = value + 23;
    return value - 41 + low;
}
