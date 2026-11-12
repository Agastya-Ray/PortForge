#include <assert.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#define BASE UINT64_C(0x1000)
#define INTEGER UINT64_C(0x8000000000000000)

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static void check_stops(void) {
    uint8_t bytes[11] = {0};
    struct pf_state state = {.gpr = {BASE + 2}, .rip = 0x100,
        .x87 = {.control = 0x37f}, .memory = {bytes, BASE, sizeof(bytes), true}};
    struct pf_x87_state saved = state.x87;
    pf_block_0(&state);
    assert(state.rip == 0x100 && state.memory_fault == PF_MEM_UNMAPPED && memcmp(&saved, &state.x87, sizeof(saved)) == 0);
    state.memory_fault = PF_MEM_OK;
    pf_block_1(&state);
    assert(state.rip == 0x100 && state.memory_fault == PF_MEM_UNMAPPED && memcmp(&saved, &state.x87, sizeof(saved)) == 0);
    state.gpr[0] = BASE + 1;
    state.memory_fault = PF_MEM_OK;
    state.memory.writable = false;
    pf_block_1(&state);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.rip == 0x100 && state.x87.status == 0);
    state.memory_fault = PF_MEM_OK;
    pf_write_le64(bytes + 1, INTEGER);
    bytes[9] = 0xff;
    bytes[10] = 0x7f;
    pf_block_0(&state);
    assert(state.x87_unsupported && state.rip == 0x100 && state.memory_fault == PF_MEM_OK);
    assert(memcmp(&saved, &state.x87, sizeof(saved)) == 0);
}

int main(void) {
    check_stops();
    uint64_t digest = PF_MEMORY_DIGEST_SEED, seed = UINT64_C(0xa231d7e5fc0864b9);
    for (unsigned op = 0; op < 3; ++op) {
        for (unsigned i = 0; i < 1000000; ++i) {
            uint64_t significand = next(&seed);
            uint16_t exponent = (uint16_t)next(&seed);
            exponent = (exponent & 0x8000) | (exponent % 0x7fff);
            if (i % 16 < 3) exponent &= 0x8000;
            if (exponent & 0x7fff) significand |= INTEGER;
            else significand &= ~INTEGER;
            if (i % 16 == 0) significand = 0;
            unsigned depth = i % 9;
            uint8_t bytes[11] = {0};
            pf_write_le64(bytes + 1, significand);
            bytes[9] = (uint8_t)exponent;
            bytes[10] = (uint8_t)(exponent >> 8);
            uint8_t native_bytes[sizeof(bytes)];
            memcpy(native_bytes, bytes, sizeof(bytes));
            uint16_t control = 0x7f | (((i / 9) % 4) << 10) | (((i / 36) % 4) << 8);
            struct pf_state state = {.gpr = {BASE + 1}, .rflags = 0x8d5,
                .x87 = {.control = control}, .memory = {bytes, BASE, sizeof(bytes), true}};
            for (unsigned j = 0; j < depth; ++j) assert(pf_x87_execute(&state.x87, PF_FLD1, 0, 0));
            if (op == 0) pf_block_0(&state);
            else if (op == 1) pf_block_1(&state);
            else pf_block_2(&state);
            assert(!state.x87_unsupported && state.memory_fault == PF_MEM_OK && state.rflags == 0x8d5);
            assert(state.rip == (op == 2 ? 4 : 2));
#if defined(__x86_64__)
            __asm__ volatile("fninit; fldcw %0" : : "m"(control));
            for (unsigned j = 0; j < depth; ++j) __asm__ volatile("fld1" : : : "st");
            if (op == 0) __asm__ volatile("fldt (%0)" : : "r"(native_bytes + 1) : "st", "memory");
            else if (op == 1) __asm__ volatile("fstpt (%0)" : : "r"(native_bytes + 1) : "st", "memory");
            else __asm__ volatile("fldt (%0); fstpt (%0)" : : "r"(native_bytes + 1) : "st", "memory");
            _Alignas(16) uint8_t snapshot[512];
            __asm__ volatile("fxsave %0; fninit" : "=m"(snapshot));
            uint16_t status = snapshot[2] | ((uint16_t)snapshot[3] << 8);
            assert((state.x87.status & 0x3aff) == (status & 0x3aff));
            assert(state.x87.occupied == snapshot[4] && memcmp(bytes, native_bytes, sizeof(bytes)) == 0);
#endif
            unsigned top = (state.x87.status & PF_X87_TOP) >> 11;
            for (unsigned j = 0; j < 8; ++j) {
                unsigned slot = (top + j) & 7;
                if (!(state.x87.occupied & (1u << slot))) continue;
#if defined(__x86_64__)
                assert(state.x87.slots[slot].significand == pf_read_le64(snapshot + 32 + 16 * j));
                uint16_t native_exp = snapshot[40 + 16 * j] | ((uint16_t)snapshot[41 + 16 * j] << 8);
                assert(state.x87.slots[slot].sign_exponent == native_exp);
#endif
                digest = pf_memory_digest(digest, state.x87.slots[slot].significand);
                digest = pf_memory_digest(digest, state.x87.slots[slot].sign_exponent);
            }
            for (size_t j = 0; j < sizeof(bytes); ++j) digest = pf_memory_digest(digest, bytes[j]);
            digest = pf_memory_digest(digest, state.x87.status & 0x3aff);
        }
    }
    pf_memory_print_digest(digest);
}
