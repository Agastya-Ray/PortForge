#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    uint64_t seed = UINT64_C(0x8b611731b96fe241);
    for (unsigned form = 0; form < 2; ++form) {
        for (unsigned i = 0; i < 1000000; ++i) {
            _Alignas(16) uint8_t bytes[48], expected[48];
            memset(bytes, 0xcc, sizeof(bytes));
            memset(expected, 0xcc, sizeof(expected));
            unsigned offset = form == 0 ? 16 * (i % 3) : i % 33;
            struct pf_xmm value = {{next(&seed), next(&seed)}};
#if defined(__x86_64__)
            if (form == 0) {
                __asm__ volatile("movdqu (%0), %%xmm0; movdqa %%xmm0, (%1); movdqa (%1), %%xmm1"
                    : : "r"(&value), "r"(expected + offset) : "xmm0", "xmm1", "memory");
            } else {
                __asm__ volatile("movdqu (%0), %%xmm0; movdqu %%xmm0, (%1); movdqu (%1), %%xmm1"
                    : : "r"(&value), "r"(expected + offset) : "xmm0", "xmm1", "memory");
            }
#else
            pf_write_le64(expected + offset, value.qword[0]);
            pf_write_le64(expected + offset + 8, value.qword[1]);
#endif
            struct pf_state state = {.memory = {bytes, 0x1000, sizeof(bytes), true}, .rflags = 0x8d5};
            state.gpr[0] = 0x1000 + offset;
            state.xmm[0] = value;
            if (form == 0) pf_block_1(&state);
            else pf_block_3(&state);
            assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
            state.xmm[0] = (struct pf_xmm){0};
            if (form == 0) pf_block_0(&state);
            else pf_block_2(&state);
            assert(memcmp(&state.xmm[0], &value, 16) == 0);
            state.xmm[0] = (struct pf_xmm){0};
            state.xmm[1] = value;
            if (form == 0) pf_block_4(&state);
            else pf_block_5(&state);
            assert(memcmp(&state.xmm[0], &value, 16) == 0);
            assert(state.rip == 12 && state.memory_fault == PF_MEM_OK && state.rflags == 0x8d5);
        }
    }
    uint8_t bytes[32] = {0}, unchanged[32] = {0};
    const uint64_t addresses[] = {0x1001, 0x1000, 0x1018};
    const enum pf_memory_fault faults[] = {PF_MEM_ALIGNMENT, PF_MEM_READ_ONLY, PF_MEM_UNMAPPED};
    for (unsigned i = 0; i < 3; ++i) {
        struct pf_state state = {.memory = {bytes, 0x1000, sizeof(bytes), false}, .rip = 0x800};
        state.xmm[0].qword[0] = UINT64_MAX;
        state.gpr[0] = addresses[i];
        if (i == 0) pf_block_1(&state);
        else pf_block_3(&state);
        assert(state.rip == 0x800 && state.memory_fault == faults[i]);
        assert(state.fault_address == addresses[i] && memcmp(bytes, unchanged, sizeof(bytes)) == 0);
    }
    puts("MOVDQA and MOVDQU passed 1,000,000 inputs per form and atomic fault checks");
}
