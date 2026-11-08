#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"

#if defined(__x86_64__)
#include <emmintrin.h>
#define NATIVE(op) __asm__ volatile(op " (%1), %0" : "+x"(lhs) : "r"(bytes) : "memory")
static struct pf_xmm reference(unsigned op, struct pf_xmm a, const uint8_t *bytes) {
    __m128i lhs;
    memcpy(&lhs, &a, 16);
    switch (op) {
    case 0: NATIVE("pxor"); break;
    case 1: NATIVE("paddd"); break;
    case 2: NATIVE("paddq"); break;
    case 3: NATIVE("psubd"); break;
    case 4: NATIVE("psubq"); break;
    }
    memcpy(&a, &lhs, 16);
    return a;
}
#endif

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    void (*const blocks[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4,
    };
    const enum pf_packed_op operations[] = {PF_PXOR, PF_PADDD, PF_PADDQ, PF_PSUBD, PF_PSUBQ};
    _Alignas(16) uint8_t data[32] = {0};
    uint64_t seed = UINT64_C(0xc029a31fa85b2603);
    for (unsigned op = 0; op < 5; ++op) {
        for (unsigned i = 0; i < 1000000; ++i) {
            struct pf_xmm a = {{next(&seed), next(&seed)}}, b = {{next(&seed), next(&seed)}};
            pf_write_le64(data + 16, b.qword[0]);
            pf_write_le64(data + 24, b.qword[1]);
            struct pf_state state = {.memory = {data, 0x1000, sizeof(data), false}, .rflags = 0x8d5};
            state.gpr[0] = 0x1010;
            state.xmm[0] = a;
            blocks[op](&state);
            struct pf_xmm expected = pf_packed(operations[op], a, b);
#if defined(__x86_64__)
            struct pf_xmm native = reference(op, a, data + 16);
            assert(memcmp(&expected, &native, 16) == 0);
#endif
            assert(memcmp(&state.xmm[0], &expected, 16) == 0);
            assert(state.rip == 4 && state.memory_fault == PF_MEM_OK && state.rflags == 0x8d5);
        }
    }
    const uint64_t addresses[] = {0x1001, 0x1020, UINT64_C(0x0000800000000000)};
    const enum pf_memory_fault faults[] = {PF_MEM_ALIGNMENT, PF_MEM_UNMAPPED, PF_MEM_NONCANONICAL};
    for (unsigned i = 0; i < 3; ++i) {
        struct pf_state state = {.memory = {data, 0x1000, sizeof(data), false}, .rip = 0x5000};
        state.gpr[0] = addresses[i];
        state.xmm[0].qword[0] = 7;
        pf_block_0(&state);
        assert(state.rip == 0x5000 && state.xmm[0].qword[0] == 7);
        assert(state.memory_fault == faults[i] && state.fault_address == addresses[i]);
    }
    puts("five SSE2 memory operations passed 1,000,000 inputs each");
}
