#include <assert.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#define BASE UINT64_C(0x1000)

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static void check_faults(void) {
    uint8_t bytes[8] = {0};
    struct pf_state state = {.gpr = {BASE + 1}, .rip = 0x100,
        .x87 = {.control = 0x37f}, .memory = {bytes, BASE, sizeof(bytes), true}};
    struct pf_x87_state before = state.x87;
    pf_block_2(&state);
    assert(state.rip == 0x100 && state.memory_fault == PF_MEM_UNMAPPED);
    assert(memcmp(&before, &state.x87, sizeof(before)) == 0);
    state.memory_fault = PF_MEM_OK;
    state.x87.control = 0x37e;
    before = state.x87;
    pf_block_0(&state);
    assert(state.rip == 0x100 && state.x87_unsupported && state.memory_fault == PF_MEM_OK);
    assert(memcmp(&before, &state.x87, sizeof(before)) == 0);
    assert(!pf_x87_load_integer(&state.x87, 0, 8));
}

int main(void) {
    check_faults();
    uint64_t digest = PF_MEMORY_DIGEST_SEED, seed = UINT64_C(0xfc3709e4a218b65d);
    const unsigned widths[] = {16, 32, 64};
    void (*const blocks[])(struct pf_state *) = {pf_block_0, pf_block_1, pf_block_2};
    for (unsigned test = 0; test < 3; ++test) {
        for (unsigned i = 0; i < 1000000; ++i) {
            unsigned width = widths[test], depth = i % 9;
            uint64_t bits = next(&seed);
            if (i % 32 == 0) bits = 0;
            if (i % 32 == 1) bits = UINT64_C(1) << (width - 1);
            if (i % 32 == 2) bits = UINT64_MAX;
            uint8_t memory[9] = {0};
            pf_write_le64(memory + 1, bits);
            const unsigned precision[] = {0, 0x200, 0x300};
            uint16_t control = 0x7f | precision[i % 3] | (((i / 3) % 4) << 10);
            struct pf_state state = {.gpr = {BASE + 1}, .rflags = 0x8d5,
                .x87 = {.control = control}, .memory = {memory, BASE, sizeof(memory), true}};
            for (unsigned j = 0; j < depth; ++j) assert(pf_x87_execute(&state.x87, PF_FLD1, 0, 0));
            blocks[test](&state);
            assert(!state.x87_unsupported && state.memory_fault == PF_MEM_OK && state.rip == 2 && state.rflags == 0x8d5);
#if defined(__x86_64__)
            __asm__ volatile("fninit; fldcw %0" : : "m"(control));
            for (unsigned j = 0; j < depth; ++j) __asm__ volatile("fld1" : : : "st");
            if (width == 16) __asm__ volatile("filds (%0)" : : "r"(memory + 1) : "st");
            else if (width == 32) __asm__ volatile("fildl (%0)" : : "r"(memory + 1) : "st");
            else __asm__ volatile("fildq (%0)" : : "r"(memory + 1) : "st");
            _Alignas(16) uint8_t snapshot[512];
            __asm__ volatile("fxsave %0; fninit" : "=m"(snapshot));
            uint16_t status = snapshot[2] | ((uint16_t)snapshot[3] << 8);
            assert((state.x87.status & 0x3aff) == (status & 0x3aff));
            assert(state.x87.occupied == snapshot[4]);
#endif
            unsigned top = (state.x87.status & PF_X87_TOP) >> 11;
            for (unsigned j = 0; j < 8; ++j) {
                unsigned slot = (top + j) & 7;
                if (!(state.x87.occupied & (1u << slot))) continue;
#if defined(__x86_64__)
                assert(state.x87.slots[slot].significand == pf_read_le64(snapshot + 32 + 16 * j));
                uint16_t exponent = snapshot[40 + 16 * j] | ((uint16_t)snapshot[41 + 16 * j] << 8);
                assert(state.x87.slots[slot].sign_exponent == exponent);
#endif
                digest = pf_memory_digest(digest, state.x87.slots[slot].significand);
                digest = pf_memory_digest(digest, state.x87.slots[slot].sign_exponent);
            }
            digest = pf_memory_digest(digest, state.x87.status & 0x3aff);
        }
    }
    pf_memory_print_digest(digest);
}
