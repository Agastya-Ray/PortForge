#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

#if defined(__x86_64__)
#define NATIVE_COMPARE(bytes) \
    __asm__ volatile("stmxcsr %0; ldmxcsr %3; movdqu %4, %%xmm0; movdqu %5, %%xmm1; " \
        "pushq $0xad7; popfq; .byte " bytes "; pushfq; popq %1; stmxcsr %2; ldmxcsr %0" \
        : "=&m"(saved), "=&r"(flags), "=m"(*mxcsr) \
        : "m"(control), "m"(left), "m"(right), "a"(address) : "xmm0", "xmm1", "cc", "memory")
#endif

static uint64_t reference(unsigned form, uint32_t *mxcsr, struct pf_xmm left,
                          struct pf_xmm right, const uint8_t *address) {
#if defined(__x86_64__)
    uint32_t saved, control = *mxcsr;
    uint64_t flags;
    switch (form) {
    case 0: NATIVE_COMPARE("0x0f,0x2e,0xc1"); break;
    case 1: NATIVE_COMPARE("0x0f,0x2e,0x00"); break;
    case 2: NATIVE_COMPARE("0x0f,0x2f,0xc1"); break;
    case 3: NATIVE_COMPARE("0x0f,0x2f,0x00"); break;
    case 4: NATIVE_COMPARE("0x66,0x0f,0x2e,0xc1"); break;
    case 5: NATIVE_COMPARE("0x66,0x0f,0x2e,0x00"); break;
    case 6: NATIVE_COMPARE("0x66,0x0f,0x2f,0xc1"); break;
    case 7: NATIVE_COMPARE("0x66,0x0f,0x2f,0x00"); break;
    default: assert(false); return 0;
    }
    return flags & PF_ARITHMETIC_FLAGS;
#else
    (void)address;
    bool wide = form >= 4;
    uint64_t bits[] = {left.qword[0], right.qword[0]};
    uint64_t exponent = wide ? UINT64_C(0x7ff0000000000000) : 0x7f800000;
    uint64_t fraction = wide ? UINT64_C(0x000fffffffffffff) : 0x007fffff;
    bool unordered = false, signalling = false;
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t mantissa = bits[i] & fraction;
        if ((bits[i] & exponent) == exponent && mantissa) {
            unordered = true;
            if (mantissa <= fraction / 2) signalling = true;
        }
    }
    if (unordered) {
        if (signalling || (form & 2)) *mxcsr |= 1;
        return 0x45;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!(bits[i] & exponent) && (bits[i] & fraction)) {
            if (*mxcsr & 0x40) bits[i] &= ~fraction;
            else *mxcsr |= 2;
        }
    }
    if (wide) {
        double a, b;
        memcpy(&a, &bits[0], sizeof(a));
        memcpy(&b, &bits[1], sizeof(b));
        return a == b ? 0x40 : a < b;
    }
    uint32_t raw[] = {(uint32_t)bits[0], (uint32_t)bits[1]};
    float a, b;
    memcpy(&a, &raw[0], sizeof(a));
    memcpy(&b, &raw[1], sizeof(b));
    return a == b ? 0x40 : a < b;
#endif
}

static void check_faults(void) {
    uint8_t bytes[8] = {0};
    struct pf_state state = {
        .gpr = {0x2001, 99}, .xmm = {{{0x7f800001, 99}}}, .rip = 0x1000,
        .rflags = 0xad7, .pending = {4, 5, 9, 64, PF_ADD}, .mxcsr = 0x1f00,
        .memory = {bytes, 0x2000, sizeof(bytes), false},
    };
    pf_block_8(&state);
    assert(state.simd_fault == PF_SIMD_INVALID && state.mxcsr == 0x1f01 && !state.memory_fault);
    assert(state.rip == 0x1000 && state.rflags == 0xad7 && state.pending.operation == PF_ADD);
    assert(state.gpr[1] == 99 && state.xmm[0].qword[0] == 0x7f800001);
    state.simd_fault = PF_SIMD_OK;
    state.mxcsr = 0x1e80;
    state.xmm[0].qword[0] = 1;
    pf_block_9(&state);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == 0x2001);
    assert(state.simd_fault == PF_SIMD_OK && state.mxcsr == 0x1e80);
    assert(state.rip == 0x1000 && state.rflags == 0xad7 && state.pending.operation == PF_ADD);
    state.memory_fault = PF_MEM_OK;
    state.gpr[0] = 0x2000;
    pf_block_9(&state);
    assert(state.simd_fault == PF_SIMD_DENORMAL && state.mxcsr == 0x1e82 && !state.memory_fault);
    assert(state.rip == 0x1000 && state.rflags == 0xad7 && state.pending.operation == PF_ADD);
    assert(state.gpr[1] == 99);
}

int main(void) {
    const uint64_t edge[][12] = {
        {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000, 0x3f800000, 0xbf800000,
         0x7f800000, 0xff800000, 0x7f800001, 0xffc00001},
        {0, UINT64_C(0x8000000000000000), 1, UINT64_C(0x8000000000000001),
         UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000), UINT64_C(0x3ff0000000000000),
         UINT64_C(0xbff0000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
         UINT64_C(0x7ff0000000000001), UINT64_C(0xfff8000000000001)},
    };
    void (*const blocks[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4, pf_block_5, pf_block_6, pf_block_7,
    };
    uint64_t seed = UINT64_C(0x4b4c7c278a04220b), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned form = 0; form < 8; ++form) {
        bool wide = form >= 4;
        for (unsigned i = 0; i < 1000000; ++i) {
            uint64_t a = next(&seed), b = next(&seed);
            if (i % 4 == 0) {
                a = edge[wide][(i / 4) % 12];
                b = edge[wide][(i / 48) % 12];
            } else if (i % 4 == 1) {
                a = edge[wide][(i / 4) % 12];
            } else if (i % 4 == 2) {
                b = edge[wide][(i / 4) % 12];
            }
            struct pf_xmm left = {{a, next(&seed)}}, right = {{b, next(&seed)}};
            uint8_t memory[13] = {0};
            unsigned offset = i % 5;
            pf_write_le64(memory + offset, b);
            uint32_t control = 0x1f80 | ((uint32_t)next(&seed) & 0xe07f);
            struct pf_state state = {
                .gpr = {0x2000 + offset}, .xmm = {left, right}, .mxcsr = control, .rflags = 0xad7,
                .pending = {9, 3, 6, 64, PF_SUB},
                .memory = {memory, 0x2000, sizeof(memory), false},
            };
            uint64_t flags = reference(form, &control, left, right, memory + offset);
            blocks[form](&state);
            assert(state.rflags == ((UINT64_C(0xad7) & ~PF_ARITHMETIC_FLAGS) | flags));
            assert(state.pending.operation == PF_NO_FLAGS && state.mxcsr == control);
            assert(memcmp(&state.xmm[0], &left, sizeof(left)) == 0);
            assert(memcmp(&state.xmm[1], &right, sizeof(right)) == 0);
            assert(state.rip == (wide ? 4 : 3) && !state.simd_fault && !state.memory_fault);
            digest = pf_memory_digest(pf_memory_digest(digest, state.rflags), state.mxcsr);
        }
    }
    check_faults();
    pf_memory_print_digest(digest);
}
