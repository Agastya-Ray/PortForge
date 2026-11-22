#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"

#if defined(__x86_64__)
#define NATIVE_MOVE(prefix) \
    __asm__ volatile("movdqu %3, %%xmm0; movdqu %4, %%xmm1; " \
                     ".byte " prefix ",0x0f,0x10,0xc1; movdqu %%xmm0, %0; " \
                     "movdqu %3, %%xmm0; .byte " prefix ",0x0f,0x11,0xc8; movdqu %%xmm0, %1; " \
                     ".byte " prefix ",0x0f,0x11,0x08; " \
                     ".byte " prefix ",0x0f,0x10,0x00; movdqu %%xmm0, %2" \
        : "=m"(copied), "=m"(reverse), "=m"(loaded) : "m"(initial), "m"(input), "a"(expected + offset) \
        : "xmm0", "xmm1", "memory")
#endif

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static void check_faults(void) {
    uint8_t memory[16];
    memset(memory, 0xff, sizeof(memory));
    void (*const blocks[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_6, pf_block_7,
    };
    const unsigned widths[] = {4, 4, 4, 4, 8, 8};
    for (unsigned form = 0; form < 6; ++form) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            if (kind == 2 && form % 2 == 0) continue;
            uint64_t address = kind == 0 ? 0x1011 - widths[form]
                : kind == 1 ? UINT64_MAX : 0x1001;
            enum pf_memory_fault fault = kind == 0 ? PF_MEM_UNMAPPED
                : kind == 1 ? PF_MEM_NONCANONICAL : PF_MEM_READ_ONLY;
            struct pf_state state = {
                .gpr = {address}, .xmm = {{{1, 2}}}, .rip = 0x800,
                .mxcsr = PF_MXCSR_RESET, .rflags = 0x8d7,
                .memory = {memory, 0x1000, sizeof(memory), kind != 2},
            };
            blocks[form](&state);
            assert(state.memory_fault == fault && state.fault_address == address);
            assert(state.rip == 0x800 && state.simd_fault == PF_SIMD_OK);
            assert(state.mxcsr == PF_MXCSR_RESET && state.rflags == 0x8d7);
            assert(state.xmm[0].qword[0] == 1 && state.xmm[0].qword[1] == 2);
            for (size_t i = 0; i < sizeof(memory); ++i) assert(memory[i] == 0xff);
        }
    }
    for (unsigned bit = 16; bit < 32; ++bit) {
        struct pf_state state = {
            .gpr = {0x1000, 99}, .rip = 0x800, .mxcsr = PF_MXCSR_RESET,
            .rflags = 0x202, .pending = {4, 5, 9, 64, PF_ADD},
            .memory = {memory, 0x1000, sizeof(memory), true},
        };
        pf_write_le32(memory, PF_MXCSR_RESET | (UINT32_C(1) << bit));
        pf_block_12(&state);
        assert(state.simd_fault == PF_SIMD_BAD_MXCSR && state.memory_fault == PF_MEM_OK);
        assert(state.gpr[1] == 99 && state.rip == 0x800 && state.mxcsr == PF_MXCSR_RESET);
        assert(state.rflags == 0x202 && state.pending.operation == PF_ADD);
    }

    struct pf_state state = {.xmm = {[14] = {{UINT64_MAX, 9}}, [15] = {{42, 17}}}};
    pf_block_10(&state);
    assert(state.xmm[14].qword[0] == UINT64_C(0xffffffff0000002a));
    assert(state.xmm[14].qword[1] == 9);
    pf_block_11(&state);
    assert(state.xmm[15].qword[0] == 42 && state.xmm[15].qword[1] == 17 && state.rip == 10);
}

int main(void) {
    uint64_t seed = UINT64_C(0x10021df69cb1d28d), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint32_t value = (uint32_t)next(&seed) & 0xffff;
        uint32_t native = value;
#if defined(__x86_64__)
        uint32_t saved;
        __asm__ volatile("stmxcsr %0; ldmxcsr %2; stmxcsr %1; ldmxcsr %0"
            : "=&m"(saved), "=m"(native) : "m"(value) : "memory");
#endif
        uint8_t bytes[13];
        memset(bytes, 0x5a, sizeof(bytes));
        pf_write_le32(bytes + 1, value);
        struct pf_state state = {
            .gpr = {0x1001}, .mxcsr = PF_MXCSR_RESET, .rflags = 0x8d7,
            .pending = {UINT64_MAX, 1, 0, 64, PF_ADD},
            .memory = {bytes, 0x1000, sizeof(bytes), true},
        };
        pf_block_0(&state);
        state.gpr[0] = 0x1007;
        pf_block_1(&state);
        assert(state.mxcsr == native && pf_read_le32(bytes + 7) == native);
        assert(bytes[0] == 0x5a && bytes[6] == 0x5a && bytes[11] == 0x5a);
        assert(state.pending.operation == PF_ADD && state.rflags == 0x8d7);
        assert(state.rip == 6 && !state.simd_fault && !state.memory_fault);
        digest = pf_memory_digest(digest, native);
    }

    void (*const blocks[])(struct pf_state *) = {
        pf_block_2, pf_block_3, pf_block_4, pf_block_5,
        pf_block_6, pf_block_7, pf_block_8, pf_block_9,
    };
    for (unsigned form = 0; form < 2; ++form) {
        unsigned width = 4 + form * 4;
        for (unsigned i = 0; i < 1000000; ++i) {
            uint8_t bytes[24], expected[24];
            memset(bytes, 0x5a, sizeof(bytes));
            memset(expected, 0x5a, sizeof(expected));
            unsigned offset = i % (sizeof(bytes) - width + 1);
            struct pf_xmm initial = {{next(&seed), next(&seed)}};
            struct pf_xmm input = {{next(&seed), next(&seed)}};
            uint64_t low = form ? input.qword[0] : (uint32_t)input.qword[0];
            struct pf_xmm loaded = {{low, 0}};
            struct pf_xmm copied = {{low | (form ? 0 : initial.qword[0] & ~UINT64_C(0xffffffff)), initial.qword[1]}};
            struct pf_xmm reverse = copied;
#if defined(__x86_64__)
            if (form == 0) { NATIVE_MOVE("0xf3"); }
            else { NATIVE_MOVE("0xf2"); }
#else
            for (unsigned byte = 0; byte < width; ++byte) {
                expected[offset + byte] = (uint8_t)(low >> (8 * byte));
            }
#endif
            struct pf_state state = {
                .gpr = {0x1000 + offset}, .xmm = {initial, input}, .mxcsr = 0xffff,
                .rflags = 0x8d7, .pending = {UINT64_MAX, 1, 0, 64, PF_ADD},
                .memory = {bytes, 0x1000, sizeof(bytes), true},
            };
            blocks[form * 4 + 2](&state);
            assert(memcmp(&state.xmm[0], &copied, 16) == 0);
            state.xmm[0] = initial;
            blocks[form * 4 + 3](&state);
            assert(memcmp(&state.xmm[0], &reverse, 16) == 0);
            blocks[form * 4 + 1](&state);
            assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
            state.xmm[0] = initial;
            blocks[form * 4](&state);
            assert(memcmp(&state.xmm[0], &loaded, 16) == 0);
            assert(memcmp(&state.xmm[1], &input, 16) == 0);
            assert(state.mxcsr == 0xffff && state.rflags == 0x8d7 && state.pending.operation == PF_ADD);
            assert(state.rip == 16 && !state.simd_fault && !state.memory_fault);
            digest = pf_memory_digest(digest, copied.qword[0]);
            digest = pf_memory_digest(digest, copied.qword[1]);
            digest = pf_memory_digest(digest, reverse.qword[0]);
            digest = pf_memory_digest(digest, loaded.qword[0]);
        }
    }
    check_faults();
    pf_memory_print_digest(digest);
}
