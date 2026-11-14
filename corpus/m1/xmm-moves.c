#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#if defined(__x86_64__)
#define NATIVE_MOVE(mnemonic) \
    __asm__ volatile("movdqu (%2), %%xmm1; " mnemonic " %%xmm1, (%3); " \
                     mnemonic " (%3), %%xmm0; movdqu %%xmm0, %0; " \
                     mnemonic " %%xmm1, %%xmm0; movdqu %%xmm0, %1" \
        : "=m"(loaded), "=m"(copied) : "r"(&value), "r"(expected + offset) \
        : "xmm0", "xmm1", "memory")
#endif

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    uint64_t seed = UINT64_C(0x8b611731b96fe241);
    uint64_t digest = PF_MEMORY_DIGEST_SEED;
    void (*const loads[])(struct pf_state *) = {pf_block_0, pf_block_2, pf_block_6, pf_block_9, pf_block_12, pf_block_15};
    void (*const stores[])(struct pf_state *) = {pf_block_1, pf_block_3, pf_block_7, pf_block_10, pf_block_13, pf_block_16};
    void (*const copies[])(struct pf_state *) = {pf_block_4, pf_block_5, pf_block_8, pf_block_11, pf_block_14, pf_block_17};
    for (unsigned form = 0; form < 6; ++form) {
        for (unsigned i = 0; i < 1000000; ++i) {
            _Alignas(16) uint8_t bytes[48], expected[48];
            memset(bytes, 0xcc, sizeof(bytes));
            memset(expected, 0xcc, sizeof(expected));
            unsigned offset = form % 2 == 0 ? 16 * (i % 3) : i % 33;
            struct pf_xmm value = {{next(&seed), next(&seed)}};
            struct pf_xmm loaded = value, copied = value;
#if defined(__x86_64__)
            switch (form) {
            case 0: NATIVE_MOVE("movdqa"); break;
            case 1: NATIVE_MOVE("movdqu"); break;
            case 2: NATIVE_MOVE("movaps"); break;
            case 3: NATIVE_MOVE("movups"); break;
            case 4: NATIVE_MOVE("movapd"); break;
            case 5: NATIVE_MOVE("movupd"); break;
            }
#else
            for (size_t byte = 0; byte < 16; ++byte) {
                expected[offset + byte] = (uint8_t)(value.qword[byte / 8] >> (8 * (byte % 8)));
            }
#endif
            struct pf_state state = {.memory = {bytes, 0x1000, sizeof(bytes), true}, .rflags = 0x8d5};
            state.gpr[0] = 0x1000 + offset;
            state.xmm[0] = value;
            if (i & 1) state.pending = (struct pf_pending){UINT64_MAX, 1, 0, 64, PF_ADD};
            struct pf_pending pending = state.pending;
            uint64_t flags = pf_read_flags(&state);
            stores[form](&state);
            assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
            state.xmm[0] = (struct pf_xmm){0};
            loads[form](&state);
            assert(memcmp(&state.xmm[0], &loaded, 16) == 0);
            digest = pf_memory_digest(digest, state.xmm[0].qword[0]);
            digest = pf_memory_digest(digest, state.xmm[0].qword[1]);
            state.xmm[0] = (struct pf_xmm){0};
            state.xmm[1] = value;
            copies[form](&state);
            assert(memcmp(&state.xmm[0], &copied, 16) == 0);
            digest = pf_memory_digest(digest, state.xmm[0].qword[0]);
            digest = pf_memory_digest(digest, state.xmm[0].qword[1]);
            for (size_t byte = 0; byte < sizeof(bytes); ++byte) {
                digest = pf_memory_digest(digest, bytes[byte]);
            }
            assert(pf_read_flags(&state) == flags && memcmp(&state.pending, &pending, sizeof(pending)) == 0);
            assert(state.rip == (form == 2 || form == 3 ? 9 : 12) && state.memory_fault == PF_MEM_OK && state.rflags == 0x8d5);
        }
    }
    uint8_t bytes[32] = {0}, unchanged[32] = {0};
    const uint64_t addresses[] = {0x1001, 0x1000, 0x1020};
    const enum pf_memory_fault faults[] = {PF_MEM_ALIGNMENT, PF_MEM_READ_ONLY, PF_MEM_UNMAPPED};
    for (unsigned form = 0; form < 6; ++form) {
    for (unsigned i = 0; i < 3; ++i) {
        if (i == 0 && form % 2 != 0) continue;
        struct pf_state state = {.memory = {bytes, 0x1000, sizeof(bytes), false}, .rip = 0x800};
        state.xmm[0].qword[0] = UINT64_MAX;
        state.gpr[0] = addresses[i];
        stores[form](&state);
        assert(state.rip == 0x800 && state.memory_fault == faults[i]);
        assert(state.fault_address == addresses[i] && memcmp(bytes, unchanged, sizeof(bytes)) == 0);
    }
    }
    pf_memory_print_digest(digest);
}
