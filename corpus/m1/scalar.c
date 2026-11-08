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

static void check_stops(void) {
    uint8_t bytes[16] = {0}, original[16] = {0};
    struct pf_state state = {
        .gpr = {BASE + 1, UINT64_MAX}, .rip = 0x100,
        .memory = {bytes, BASE, sizeof(bytes), false},
    };
    pf_block_3(&state);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.fault_address == BASE + 1);
    assert(state.rip == 0x100 && memcmp(bytes, original, sizeof(bytes)) == 0);
    for (unsigned width = 4; width <= 8; width += 4) {
        state = (struct pf_state){
            .gpr = {UINT64_MAX, BASE + sizeof(bytes) - width + 1}, .rip = 0x100,
            .memory = {bytes, BASE, sizeof(bytes), true},
        };
        if (width == 4) pf_block_9(&state);
        else pf_block_8(&state);
        assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == state.gpr[1]);
        assert(state.rip == 0x100 && state.gpr[0] == UINT64_MAX);
    }
}

#if defined(__x86_64__)
#define NATIVE_FLAGS(operation) \
    __asm__ volatile(operation "; pushfq; popq %1" : "+r"(native), "=r"(flags) : : "cc")
#endif

int main(void) {
    check_stops();
    uint64_t seed = UINT64_C(0xd1b48efea3612db3), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed);
        if (i % 16 == 0) a = 0;
        if (i % 16 == 1) a = UINT64_C(0x8000000000000000);
        if (i % 16 == 2) b = UINT32_MAX;
        if (i % 16 == 3) b = INT32_MAX;
        struct pf_state state = {.gpr = {UINT64_MAX, a, b}, .rflags = 0x8d5};
        pf_block_0(&state);
        assert(state.gpr[0] == a && state.gpr[1] == (uint32_t)b);
        pf_block_1(&state);
        assert(state.gpr[0] == UINT64_C(0x1122334455667788));
        pf_block_2(&state);
        assert(state.gpr[0] == UINT64_MAX && state.gpr[1] == UINT32_MAX);
        assert(state.rip == 27 && pf_read_flags(&state) == 0x8d5);
        digest = pf_memory_digest(digest, state.gpr[0]);
        digest = pf_memory_digest(digest, state.gpr[1]);

        uint8_t bytes[16] = {0};
        state = (struct pf_state){
            .gpr = {BASE + 1, a, b}, .rflags = 0x8d5,
            .memory = {bytes, BASE, sizeof(bytes), true},
        };
        pf_block_3(&state);
        assert(state.gpr[2] == ((b & UINT32_MAX) << 32 | (a & UINT32_MAX)));
        assert(state.gpr[1] == (uint32_t)b && state.rip == 12);
#if defined(__x86_64__)
        uint8_t native_bytes[16] = {0};
        uint64_t loaded, low;
        __asm__ volatile("movq %3, (%2); movl %k4, 4(%2); movq (%2), %0; movl 4(%2), %k1"
                         : "=&r"(loaded), "=r"(low) : "r"(native_bytes + 1), "r"(a), "r"(b) : "memory");
        assert(state.gpr[2] == loaded && state.gpr[1] == low);
        assert(memcmp(bytes, native_bytes, sizeof(bytes)) == 0);
#endif
        digest = pf_memory_digest(digest, state.gpr[2]);
        pf_block_4(&state);
        assert(pf_read_le64(bytes + 1) == UINT64_C(0x12345678ffffffff));
        assert(state.memory_fault == PF_MEM_OK && pf_read_flags(&state) == 0x8d5);
        digest = pf_memory_digest(digest, pf_read_le64(bytes + 1));

        state = (struct pf_state){.gpr = {a, b}};
        pf_block_5(&state);
        assert(state.gpr[0] == a - 1);
#if defined(__x86_64__)
        uint64_t native = a, flags;
        NATIVE_FLAGS("addq $-1, %0");
        assert(state.gpr[0] == native && pf_read_flags(&state) == (flags & PF_ARITHMETIC_FLAGS));
#endif
        digest = pf_memory_digest(digest, pf_read_flags(&state));
        pf_block_6(&state);
        assert(state.gpr[1] == (uint32_t)(b + 1));
#if defined(__x86_64__)
        native = b;
        NATIVE_FLAGS("subl $-1, %k0");
        assert(state.gpr[1] == native && pf_read_flags(&state) == (flags & PF_ARITHMETIC_FLAGS));
#endif
        digest = pf_memory_digest(digest, pf_read_flags(&state));
        pf_block_7(&state);
        assert(state.gpr[0] == a - 1 && state.rip == 14);
#if defined(__x86_64__)
        native = a - 1;
        NATIVE_FLAGS("cmpq $-2147483648, %0");
        assert(pf_read_flags(&state) == (flags & PF_ARITHMETIC_FLAGS));
#endif
        digest = pf_memory_digest(digest, pf_read_flags(&state));
    }
    pf_memory_print_digest(digest);
}
