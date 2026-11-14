#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

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
    case 5: NATIVE("pand"); break;
    case 6: NATIVE("pandn"); break;
    case 7: NATIVE("por"); break;
    case 8: NATIVE("pcmpeqd"); break;
    case 9: NATIVE("pcmpgtd"); break;
    case 10: NATIVE("paddb"); break;
    case 11: NATIVE("paddw"); break;
    case 12: NATIVE("psubb"); break;
    case 13: NATIVE("psubw"); break;
    case 14: NATIVE("paddsb"); break;
    case 15: NATIVE("paddsw"); break;
    case 16: NATIVE("paddusb"); break;
    case 17: NATIVE("paddusw"); break;
    case 18: NATIVE("psubsb"); break;
    case 19: NATIVE("psubsw"); break;
    case 20: NATIVE("psubusb"); break;
    case 21: NATIVE("psubusw"); break;
    case 22: NATIVE("pmullw"); break;
    case 23: NATIVE("pmulhw"); break;
    case 24: NATIVE("pmulhuw"); break;
    case 25: NATIVE("pmuludq"); break;
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
        pf_block_0,
        pf_block_1,
        pf_block_2,
        pf_block_3,
        pf_block_4,
        pf_block_5,
        pf_block_6,
        pf_block_7,
        pf_block_8,
        pf_block_9,
        pf_block_10,
        pf_block_11,
        pf_block_12,
        pf_block_13,
        pf_block_14,
        pf_block_15,
        pf_block_16,
        pf_block_17,
        pf_block_18,
        pf_block_19,
        pf_block_20,
        pf_block_21,
        pf_block_22,
        pf_block_23,
        pf_block_24,
        pf_block_25,

    };
    _Alignas(16) uint8_t data[32] = {0};
    uint64_t seed = UINT64_C(0xc029a31fa85b2603);
    uint64_t digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned op = 0; op < 26; ++op) {
        for (unsigned i = 0; i < 1000000; ++i) {
            struct pf_xmm a = {{next(&seed), next(&seed)}}, b = {{next(&seed), next(&seed)}};
            pf_write_le64(data + 16, b.qword[0]);
            pf_write_le64(data + 24, b.qword[1]);
            struct pf_state state = {.memory = {data, 0x1000, sizeof(data), false}, .rflags = 0x8d5};
            state.gpr[0] = 0x1010;
            state.xmm[0] = a;
            if (i & 1) state.pending = (struct pf_pending){UINT64_MAX, 1, 0, 64, PF_ADD};
            struct pf_state old = state;
            blocks[op](&state);
#if defined(__x86_64__)
            struct pf_xmm native = reference(op, a, data + 16);
            assert(memcmp(&state.xmm[0], &native, 16) == 0);
#endif
            assert(state.rip == 4 && state.memory_fault == PF_MEM_OK && state.rflags == 0x8d5);
            assert(pf_read_flags(&state) == pf_read_flags(&old) && memcmp(&state.pending, &old.pending, sizeof(old.pending)) == 0);
            digest = pf_memory_digest(digest, state.xmm[0].qword[0]);
            digest = pf_memory_digest(digest, state.xmm[0].qword[1]);
        }
    }
    const uint64_t addresses[] = {0x1001, 0x1020, UINT64_C(0x0000800000000000)};
    const enum pf_memory_fault faults[] = {PF_MEM_ALIGNMENT, PF_MEM_UNMAPPED, PF_MEM_NONCANONICAL};
    for (unsigned op = 0; op < 26; ++op) {
    for (unsigned i = 0; i < 3; ++i) {
        struct pf_state state = {.memory = {data, 0x1000, sizeof(data), false}, .rip = 0x5000};
        state.gpr[0] = addresses[i];
        state.xmm[0].qword[0] = 7;
        struct pf_state old = state;
        blocks[op](&state);
        assert(memcmp(state.gpr, old.gpr, sizeof(state.gpr)) == 0 && memcmp(state.xmm, old.xmm, sizeof(state.xmm)) == 0);
        assert(state.rflags == old.rflags && memcmp(&state.pending, &old.pending, sizeof(state.pending)) == 0);
        assert(state.rip == 0x5000 && state.xmm[0].qword[0] == 7);
        assert(state.memory_fault == faults[i] && state.fault_address == addresses[i]);
    }
    }
    pf_memory_print_digest(digest);
}
