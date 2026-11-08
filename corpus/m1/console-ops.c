#include <assert.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#define LOGIC_FLAGS (PF_ARITHMETIC_FLAGS & ~UINT64_C(0x10))
#define BASE UINT64_C(0x1000)

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static void check_stack(void) {
    uint8_t bytes[16] = {0};
    struct pf_state state = {
        .gpr = {[4] = BASE + 16}, .rflags = 0x8d5,
        .memory = {bytes, BASE, sizeof(bytes), true},
    };
    pf_block_11(&state);
    assert(state.gpr[4] == BASE + 16 && state.gpr[1] == BASE + 16);
    assert(pf_read_le64(bytes + 8) == BASE + 16 && state.rip == 2);
    state.gpr[0] = BASE + 8;
    pf_block_12(&state);
    assert(state.gpr[4] == BASE + 8 && state.rip == 4 && pf_read_flags(&state) == 0x8d5);
    uint8_t saved[sizeof(bytes)];
    memcpy(saved, bytes, sizeof(saved));
    state.memory.writable = false;
    pf_block_11(&state);
    assert(state.gpr[4] == BASE + 8 && state.rip == 4 && state.memory_fault == PF_MEM_READ_ONLY);
    assert(memcmp(bytes, saved, sizeof(saved)) == 0);
    state.memory_fault = PF_MEM_OK;
    state.gpr[1] = BASE + 13;
    pf_block_8(&state);
    assert(state.rip == 4 && state.memory_fault == PF_MEM_UNMAPPED && pf_read_flags(&state) == 0x8d5);
    state.memory_fault = PF_MEM_OK;
    pf_block_9(&state);
    assert(state.rip == 4 && state.memory_fault == PF_MEM_UNMAPPED && state.gpr[0] == BASE + 8);
    state.memory_fault = PF_MEM_OK;
    pf_block_10(&state);
    assert(state.rip == 8 && state.memory_fault == PF_MEM_OK);
}

static uint64_t check_conditions(uint64_t digest) {
    for (uint64_t flags = 0; flags <= PF_ARITHMETIC_FLAGS; ++flags) {
        if (flags & ~PF_ARITHMETIC_FLAGS) continue;
#if defined(__x86_64__)
        uint8_t taken[16];
        __asm__ volatile("pushq %[flags]; popfq; "
            "seto 0(%[out]); setno 1(%[out]); setb 2(%[out]); setae 3(%[out]); "
            "sete 4(%[out]); setne 5(%[out]); setbe 6(%[out]); seta 7(%[out]); "
            "sets 8(%[out]); setns 9(%[out]); setp 10(%[out]); setnp 11(%[out]); "
            "setl 12(%[out]); setge 13(%[out]); setle 14(%[out]); setg 15(%[out])"
            : : [flags] "r"(flags | 2), [out] "r"(taken) : "cc", "memory");
#endif
        for (unsigned code = 0; code < 16; ++code) {
            bool branch = pf_condition(flags, code);
#if defined(__x86_64__)
            assert(branch == (taken[code] != 0));
#endif
            digest = pf_memory_digest(digest, branch);
        }
    }
    return digest;
}

#if defined(__x86_64__)
#define CHECK_NATIVE(operation, result_index, mask) do { \
    uint64_t native = (result_index) == 0 ? a : b, flags; \
    __asm__ volatile(operation "; pushfq; popq %1" \
        : "+&r"(native), "=r"(flags) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(bytes) : "cc", "memory"); \
    assert(state.gpr[result_index] == native); \
    assert((pf_read_flags(&state) & (mask)) == (flags & (mask))); \
} while (0)
#endif

int main(void) {
    check_stack();
    uint64_t digest = check_conditions(PF_MEMORY_DIGEST_SEED);
    uint64_t seed = UINT64_C(0x217c590efaa3418f);
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed), c = next(&seed), d = next(&seed);
        if (i % 32 == 0) a = b;
        if (i % 32 == 1) a = INT32_MAX;
        if (i % 32 == 2) b = UINT64_C(0x8000000000000000);
        if (i % 32 == 3) c = d;
        if (i % 32 == 4) b = UINT32_MAX;
        uint8_t bytes[8];
        pf_write_le64(bytes, b);
        const struct pf_state initial = {.gpr = {a, b, c, d}};
        struct pf_state state;
        void (*const blocks[])(struct pf_state *) = {
            pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4,
            pf_block_5, pf_block_6, pf_block_7, pf_block_8, pf_block_9,
        };
        for (unsigned test = 0; test < sizeof(blocks) / sizeof(blocks[0]); ++test) {
            state = initial;
            if (test >= 8) {
                state.gpr[1] = BASE;
                state.memory = (struct pf_memory){bytes, BASE, sizeof(bytes), true};
            }
            blocks[test](&state);
            assert(state.memory_fault == PF_MEM_OK && state.rip > 0);
#if defined(__x86_64__)
            switch (test) {
            case 0: CHECK_NATIVE("xorq %3, %0", 0, LOGIC_FLAGS); break;
            case 1: CHECK_NATIVE("andl $0x7fffffff, %k0", 0, LOGIC_FLAGS); break;
            case 2: CHECK_NATIVE("orl %k3, %k0", 0, LOGIC_FLAGS); break;
            case 3: CHECK_NATIVE("testl %k3, %k0", 0, LOGIC_FLAGS); break;
            case 4: CHECK_NATIVE("cmpq %3, %2; incl %k0", 0, PF_ARITHMETIC_FLAGS); break;
            case 5: CHECK_NATIVE("cmpq %3, %2; decq %0", 1, PF_ARITHMETIC_FLAGS); break;
            case 6: CHECK_NATIVE("cmpq %5, %4; cmovbl %k3, %k0", 0, PF_ARITHMETIC_FLAGS); break;
            case 7: CHECK_NATIVE("cmpq %5, %4; cmovgq %3, %0", 0, PF_ARITHMETIC_FLAGS); break;
            case 8: CHECK_NATIVE("cmpl $-1, (%6)", 0, PF_ARITHMETIC_FLAGS); break;
            case 9: CHECK_NATIVE("addq (%6), %0", 0, PF_ARITHMETIC_FLAGS); break;
            }
#endif
            digest = pf_memory_digest(digest, state.gpr[0]);
            digest = pf_memory_digest(digest, state.gpr[1]);
            digest = pf_memory_digest(digest, pf_read_flags(&state) & (test < 4 ? LOGIC_FLAGS : PF_ARITHMETIC_FLAGS));
        }
    }
    pf_memory_print_digest(digest);
}
