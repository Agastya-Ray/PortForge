#include <assert.h>
#include "lifted.c"
#include "memory-evidence.h"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    void (*const blocks[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4,
        pf_block_5, pf_block_6, pf_block_7, pf_block_8, pf_block_9,
    };
    uint64_t seed = UINT64_C(0x85c4b290fda67e31), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t value = next(&seed), flags = next(&seed) & PF_ARITHMETIC_FLAGS;
        unsigned input_count = i & 255;
        if (i % 32 == 0) value = UINT64_C(0x8000000080000000);
        for (unsigned test = 0; test < 10; ++test) {
            unsigned width = test & 1 ? 64 : 32;
            unsigned raw_count = test < 6 ? input_count : (unsigned[]){0, 1, 255, 63}[test - 6];
            unsigned count = raw_count & (width - 1);
            struct pf_state state = {.gpr = {value, input_count}, .rflags = flags};
            blocks[test](&state);
            uint64_t observed = !count ? PF_ARITHMETIC_FLAGS : count == 1 ? UINT64_C(0x8c5) : UINT64_C(0xc5);
#if defined(__x86_64__)
            uint64_t native = value, native_flags;
#define SHIFT(operation) __asm__ volatile("pushq %[initial]; popfq; " operation "; pushfq; popq %[flags]" \
    : [value] "+r"(native), [flags] "=&r"(native_flags) : [initial] "r"(flags | 2), "c"(raw_count) : "cc", "memory")
            switch (test) {
            case 0: SHIFT("shll %%cl, %k[value]"); break;
            case 1: SHIFT("shlq %%cl, %[value]"); break;
            case 2: SHIFT("shrl %%cl, %k[value]"); break;
            case 3: SHIFT("shrq %%cl, %[value]"); break;
            case 4: SHIFT("sarl %%cl, %k[value]"); break;
            case 5: SHIFT("sarq %%cl, %[value]"); break;
            case 6: SHIFT("shll $0, %k[value]"); break;
            case 7: SHIFT("shlq $1, %[value]"); break;
            case 8: SHIFT("shrl $255, %k[value]"); break;
            default: SHIFT("sarq $63, %[value]"); break;
            }
#undef SHIFT
            assert(native == state.gpr[0]);
            assert((native_flags & observed) == (pf_read_flags(&state) & observed));
#endif
            assert(state.gpr[1] == input_count);
            digest = pf_memory_digest(digest, state.gpr[0]);
            digest = pf_memory_digest(digest, pf_read_flags(&state) & observed);
        }
    }
    struct pf_state pending = {.gpr = {1, 0}, .pending = {1, 2, 3, 64, PF_ADD}};
    uint64_t expected = pf_read_flags(&pending);
    pf_block_0(&pending);
    assert(pf_read_flags(&pending) == expected && pending.pending.operation == PF_ADD);
    pf_memory_print_digest(digest);
}
