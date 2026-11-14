#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#define BASE UINT64_C(0x1000)
struct test_case {
    void (*block)(struct pf_state *);
    const char *name;
    unsigned width, memory, write, logic, noflags;
};
#if defined(__x86_64__)
#define NATIVE_BYTES(bytes_) __asm__ volatile("pushq %[before]; popfq; .byte " bytes_ "; pushfq; popq %[after]" \
    : "+a"(*a), "+c"(*b), [after] "=&r"(*flags) \
    : "d"(bytes), [before] "r"(*flags | 2) : "cc", "memory")
#endif
#include "scalar-storage-cases.h"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17;
    return *seed;
}

static void faults(void) {
    uint8_t bytes[16] = {0}, saved[16];
    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
        const struct test_case *t = cases + test;
        if (!t->memory) continue;
        for (unsigned mode = 0; mode < 3; ++mode) {
            if (mode == 1 && !t->write) continue;
            struct pf_state state = {
                .gpr = {UINT64_MAX, 0x1122334455667788, BASE}, .rip = 0x5000,
                .rflags = 0x202, .pending = {UINT64_MAX, 1, 0, 64, PF_ADD},
                .memory = {bytes, BASE, sizeof(bytes), mode != 1},
            };
            if (mode == 0) state.gpr[2] = BASE + sizeof(bytes) - t->width / 8 + 1;
            if (mode == 2) state.gpr[2] = UINT64_MAX;
            const struct pf_state old = state;
            memcpy(saved, bytes, sizeof(bytes));
            t->block(&state);
            enum pf_memory_fault expected = mode == 1 ? PF_MEM_READ_ONLY :
                (mode == 2 && t->width > 8 ? PF_MEM_NONCANONICAL : PF_MEM_UNMAPPED);
            if (state.memory_fault != expected) {
                fprintf(stderr, "fault %s mode %u: got %u at %" PRIx64 "\n", t->name, mode, state.memory_fault, state.fault_address);
                abort();
            }
            assert(state.fault_address == old.gpr[2] && state.rip == old.rip);
            assert(memcmp(state.gpr, old.gpr, sizeof(state.gpr)) == 0);
            assert(state.rflags == old.rflags && memcmp(&state.pending, &old.pending, sizeof(state.pending)) == 0);
            assert(memcmp(bytes, saved, sizeof(bytes)) == 0);
        }
    }
}

int main(void) {
    faults();
    uint64_t seed = UINT64_C(0x89d32cfa61b7e504), digest = PF_MEMORY_DIGEST_SEED;
    const uint64_t edges[] = {0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, UINT32_MAX, UINT64_MAX, UINT64_C(0x8000000000000000)};
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed), memory_value = next(&seed);
        if (i % 4 == 0) { a = edges[(i / 4) % 11]; b = edges[(i / 44) % 11]; memory_value = edges[(i / 484) % 11]; }
        for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
            const struct test_case *t = cases + test;
            uint8_t bytes[16], native_bytes[16];
            pf_write_le64(bytes, memory_value); pf_write_le64(bytes + 8, ~memory_value);
            memcpy(native_bytes, bytes, sizeof(bytes));
            struct pf_state state = {
                .gpr = {a, b, BASE}, .rip = 0x5000,
                .rflags = 0x202 | (next(&seed) & PF_ARITHMETIC_FLAGS),
                .memory = {bytes, BASE, sizeof(bytes), t->write != 0},
            };
            if (i % 3 == 0) state.pending = (struct pf_pending){a, b, a + b, 64, PF_ADD};
            const struct pf_state old = state;
            uint64_t before = pf_read_flags(&state);
            t->block(&state);
            uint64_t after = pf_read_flags(&state);
            assert(state.memory_fault == PF_MEM_OK && state.rip > old.rip && state.gpr[2] == BASE);
            assert((before & ~PF_ARITHMETIC_FLAGS) == (after & ~PF_ARITHMETIC_FLAGS));
            if (t->noflags) {
                assert(after == before && state.rflags == old.rflags);
                assert(memcmp(&state.pending, &old.pending, sizeof(state.pending)) == 0);
            }
#if defined(__x86_64__)
            uint64_t native_a = a, native_b = b, flags = before;
            native(test, &native_a, &native_b, native_bytes, &flags);
            uint64_t mask = PF_ARITHMETIC_FLAGS & (t->logic ? ~UINT64_C(0x10) : UINT64_MAX);
            if (state.gpr[0] != native_a || state.gpr[1] != native_b || (after & mask) != (flags & mask)
                || memcmp(bytes, native_bytes, sizeof(bytes)) != 0) {
                fprintf(stderr, "%s sample %u: a=%016" PRIx64 " b=%016" PRIx64 " flags=%" PRIx64 "/%" PRIx64 "\n", t->name, i, a, b, after, flags);
                abort();
            }
#endif
            digest = pf_memory_digest(digest, state.gpr[0]); digest = pf_memory_digest(digest, state.gpr[1]);
            digest = pf_memory_digest(digest, after); digest = pf_memory_digest(digest, pf_read_le64(bytes));
            digest = pf_memory_digest(digest, pf_read_le64(bytes + 8));
        }
    }
    pf_memory_print_digest(digest);
}
