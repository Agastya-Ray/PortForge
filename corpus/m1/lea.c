#include <assert.h>
#include <stdio.h>
#include "lifted.c"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    uint64_t seed = UINT64_C(0xb018c145d92fa603);
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t base = next(&seed), index = next(&seed), rip = next(&seed);
        struct pf_state state = {.rip = rip, .rflags = 0x8d5};
        state.gpr[3] = base;
        state.gpr[1] = index;
        uint64_t full = base + index * 4 - 16;
#if defined(__x86_64__)
        uint64_t native;
        __asm__("leaq -16(%1,%2,4), %0" : "=r"(native) : "r"(base), "r"(index));
        assert(native == full);
        __asm__("leaq -16(%k1,%k2,4), %0" : "=r"(native) : "r"(base), "r"(index));
        assert(native == (uint32_t)full);
        __asm__("leal -16(%1,%2,4), %k0" : "=r"(native) : "r"(base), "r"(index));
        assert(native == (uint32_t)full);
#endif
        pf_block_0(&state);
        assert(state.gpr[0] == full && state.rip == rip + 5);
        state.rip = rip;
        pf_block_1(&state);
        assert(state.gpr[0] == (uint32_t)full && state.rip == rip + 6);
        state.rip = rip;
        pf_block_2(&state);
        assert(state.gpr[0] == (uint32_t)full && state.rip == rip + 4);
        state.rip = rip;
        pf_block_3(&state);
        assert(state.gpr[0] == rip + 7 - 16 && state.rip == rip + 7);
        state.rip = rip;
        pf_block_4(&state);
        assert(state.gpr[0] == (uint32_t)(rip + 8 - 16) && state.rip == rip + 8);
        assert(state.rflags == 0x8d5 && state.gpr[3] == base && state.gpr[1] == index);
    }
    puts("LEA address and destination widths passed 1,000,000 inputs");
}
