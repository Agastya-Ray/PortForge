#include <assert.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#define BASE UINT64_C(0x1000)
#define PRODUCT_FLAGS UINT64_C(0x801)

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static bool memory_source(unsigned test) {
    return (test >= 8 && test <= 11) || test == 18 || test == 19
        || test == 22 || test == 23 || test == 26 || test == 27;
}

static bool product(unsigned test) {
    return (test >= 16 && test <= 28) || test == 31 || test == 35
        || (test >= 37 && test <= 39);
}

static void (*const blocks[])(struct pf_state *) = {
    pf_block_0, pf_block_1, pf_block_2, pf_block_3,
    pf_block_4, pf_block_5, pf_block_6, pf_block_7,
    pf_block_8, pf_block_9, pf_block_10, pf_block_11,
    pf_block_12, pf_block_13, pf_block_14, pf_block_15,
    pf_block_16, pf_block_17, pf_block_18, pf_block_19,
    pf_block_20, pf_block_21, pf_block_22, pf_block_23,
    pf_block_24, pf_block_25, pf_block_26, pf_block_27,
    pf_block_28, pf_block_29, pf_block_30, pf_block_31,
    pf_block_32, pf_block_33, pf_block_34, pf_block_35,
    pf_block_36, pf_block_37, pf_block_38, pf_block_39,
    pf_block_40, pf_block_41, pf_block_42, pf_block_43,
};

#if defined(__x86_64__)
struct oracle { uint64_t low, high, flags; };
static struct oracle native(unsigned test, const struct pf_state *initial, const uint8_t *bytes) {
    struct oracle out = {initial->gpr[0], initial->gpr[2], 0};
    uint64_t before = pf_read_flags(initial) | 2;
#define RUN(instruction) __asm__ volatile("pushq %[before]; popfq; " instruction "; pushfq; popq %[flags]" \
    : [lo] "+&r"(out.low), [hi] "+&r"(out.high), [flags] "=&r"(out.flags) \
    : [b] "r"(initial->gpr[1]), [d] "r"(initial->gpr[3]), [mem] "r"(bytes), [before] "r"(before) \
    : "cc", "memory")
    switch (test) {
    case 0: case 29: RUN("adcq %[b], %[lo]"); break;
    case 1: RUN("adcl %k[b], %k[lo]"); break;
    case 2: RUN("sbbq %[b], %[lo]"); break;
    case 3: case 30: RUN("sbbl %k[b], %k[lo]"); break;
    case 4: RUN("adcq $-1, %[lo]"); break;
    case 5: RUN("adcl $0x80000000, %k[lo]"); break;
    case 6: RUN("sbbq $-1, %[lo]"); break;
    case 7: RUN("sbbl $0x80000000, %k[lo]"); break;
    case 8: RUN("adcq (%[mem]), %[lo]"); break;
    case 9: RUN("adcl (%[mem]), %k[lo]"); break;
    case 10: RUN("sbbq (%[mem]), %[lo]"); break;
    case 11: RUN("sbbl (%[mem]), %k[lo]"); break;
    case 12: RUN("negq %[lo]"); break;
    case 13: RUN("negl %k[lo]"); break;
    case 14: RUN("notq %[lo]"); break;
    case 15: RUN("notl %k[lo]"); break;
    case 16: RUN("imulq %[b], %[lo]"); break;
    case 17: RUN("imull %k[b], %k[lo]"); break;
    case 18: RUN("imulq (%[mem]), %[lo]"); break;
    case 19: RUN("imull (%[mem]), %k[lo]"); break;
    case 20: RUN("imulq $-1, %[b], %[lo]"); break;
    case 21: RUN("imull $-128, %k[b], %k[lo]"); break;
    case 22: RUN("imulq $-1, (%[mem]), %[lo]"); break;
    case 23: RUN("imull $-128, (%[mem]), %k[lo]"); break;
    case 24: RUN("imulq $-2147483648, %[b], %[lo]"); break;
    case 25: RUN("imull $-2147483648, %k[b], %k[lo]"); break;
    case 26: RUN("imulq $-2147483648, (%[mem]), %[lo]"); break;
    case 27: RUN("imull $-2147483648, (%[mem]), %k[lo]"); break;
    case 28: RUN("imulq %[lo], %[lo]"); break;
    case 31: RUN("imulq $-128, %[b], %[lo]"); break;
    case 32: RUN("addq %[b], %[lo]; adcq %[d], %[hi]"); break;
    case 33: RUN("subq %[b], %[lo]; sbbq %[d], %[hi]"); break;
    case 34: RUN("cmpq %[b], %[lo]; notq %[lo]"); break;
    case 35: RUN("cmpq %[b], %[lo]; imulq %[b], %[lo]"); break;
    case 36: RUN("adcq %[lo], %[lo]; sbbq %[lo], %[lo]"); break;
    case 37: RUN("imulq $-1, %[lo], %[lo]"); break;
    case 38: RUN("imulq $0, %[b], %[lo]"); break;
    case 39: RUN("imulq $127, %[b], %[lo]"); break;
    case 40: RUN("sbbq %[lo], %[lo]"); break;
    case 41: RUN("adcq %[lo], %[lo]"); break;
    case 42: RUN("adcq $0, %[lo]"); break;
    case 43: RUN("sbbq $0, %[lo]"); break;
    default: assert(false);
    }
#undef RUN
    return out;
}
#endif

static void faults(void) {
    uint8_t bytes[16] = {0}, saved[16];
    for (unsigned test = 0; test < sizeof(blocks) / sizeof(blocks[0]); ++test) {
        if (!memory_source(test)) continue;
        for (unsigned offset = 13; offset <= 17; ++offset) {
            struct pf_state state = {
                .gpr = {UINT64_MAX, 3, BASE + offset}, .rip = 0x5000,
                .rflags = 0x202, .pending = {UINT64_MAX, 1, 0, 64, PF_ADD},
                .memory = {bytes, BASE, sizeof(bytes), true},
            };
            struct pf_state old = state;
            memcpy(saved, bytes, sizeof(bytes));
            blocks[test](&state);
            assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == BASE + offset);
            assert(state.rip == old.rip && memcmp(state.gpr, old.gpr, sizeof(state.gpr)) == 0);
            assert(state.rflags == old.rflags && memcmp(&state.pending, &old.pending, sizeof(state.pending)) == 0);
            assert(memcmp(bytes, saved, sizeof(bytes)) == 0);
        }
    }
}

int main(void) {
    faults();
    const uint64_t edges[] = {0, 1, 2, 127, 128, UINT32_MAX, UINT64_MAX,
        UINT64_C(0x7fffffff), UINT64_C(0x80000000), UINT64_C(0xffffffff80000000),
        UINT64_C(0x7fffffffffffffff), UINT64_C(0x8000000000000000)};
    uint64_t digest = PF_MEMORY_DIGEST_SEED, seed = UINT64_C(0x836c5b17c29da140);
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed), c = next(&seed), d = next(&seed);
        if (i % 4 == 0) {
            a = edges[(i / 4) % 12];
            b = edges[(i / 48) % 12];
        } else if (i % 4 == 1) {
            a &= UINT64_C(0xffff);
            b &= UINT64_C(0xffff);
            if (a & UINT64_C(0x8000)) a |= ~UINT64_C(0xffff);
            if (b & UINT64_C(0x8000)) b |= ~UINT64_C(0xffff);
        } else if (i % 4 == 2) b = a;
        uint8_t bytes[8];
        pf_write_le64(bytes, b);
        for (unsigned test = 0; test < sizeof(blocks) / sizeof(blocks[0]); ++test) {
            struct pf_state initial = {
                .gpr = {a, b, c, d, 0, 0, 0, 0, a, b},
                .rflags = 0x202 | (next(&seed) & PF_ARITHMETIC_FLAGS),
                .memory = {bytes, BASE, sizeof(bytes), false},
            };
            if (memory_source(test)) initial.gpr[2] = BASE;
            if (i % 3 == 0) initial.pending = (struct pf_pending){a, b, a + b, 64, PF_ADD};
            uint64_t before = pf_read_flags(&initial);
            struct pf_state state = initial;
            blocks[test](&state);
            uint64_t after = pf_read_flags(&state);
            assert(state.memory_fault == PF_MEM_OK && state.rip > 0);
            assert((after & ~PF_ARITHMETIC_FLAGS) == (before & ~PF_ARITHMETIC_FLAGS));
            if (test == 14 || test == 15) {
                assert(after == before);
                assert(memcmp(&state.pending, &initial.pending, sizeof(state.pending)) == 0);
            }
            if (product(test) && test != 35) assert((after & ~PRODUCT_FLAGS) == (before & ~PRODUCT_FLAGS));
#if defined(__x86_64__)
            struct oracle want = native(test, &initial, bytes);
            uint64_t mask = product(test) ? PRODUCT_FLAGS : PF_ARITHMETIC_FLAGS;
            if (test >= 29 && test <= 31) {
                assert(state.gpr[8] == want.low && state.gpr[0] == a);
            } else {
                assert(state.gpr[0] == want.low && state.gpr[2] == want.high);
            }
            assert((after & mask) == (want.flags & mask));
#endif
            digest = pf_memory_digest(digest, state.gpr[0]);
            digest = pf_memory_digest(digest, state.gpr[2]);
            digest = pf_memory_digest(digest, state.gpr[8]);
            digest = pf_memory_digest(digest, after);
        }
    }
    pf_memory_print_digest(digest);
}
