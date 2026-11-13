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

int main(void) {
    void (*const extensions[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4, pf_block_5, pf_block_6,
        pf_block_7, pf_block_8, pf_block_9, pf_block_10, pf_block_11, pf_block_12, pf_block_13,
    };
    void (*const conditions[])(struct pf_state *) = {
        pf_block_14, pf_block_15, pf_block_16, pf_block_17, pf_block_18, pf_block_19, pf_block_20, pf_block_21,
        pf_block_22, pf_block_23, pf_block_24, pf_block_25, pf_block_26, pf_block_27, pf_block_28, pf_block_29,
        pf_block_30, pf_block_31, pf_block_32, pf_block_33, pf_block_34, pf_block_35, pf_block_36, pf_block_37,
        pf_block_38, pf_block_39, pf_block_40, pf_block_41, pf_block_42, pf_block_43, pf_block_44, pf_block_45,
        pf_block_46, pf_block_47, pf_block_48, pf_block_49, pf_block_50, pf_block_51, pf_block_52, pf_block_53,
        pf_block_54, pf_block_55, pf_block_56, pf_block_57, pf_block_58, pf_block_59, pf_block_60, pf_block_61,
    };
    uint64_t seed = UINT64_C(0x796da5b18c423fe0), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t value = next(&seed), original = next(&seed), flags = next(&seed) & 0x8d5;
        uint8_t bytes[9] = {0};
        pf_write_le64(bytes + 1, value);
        for (unsigned test = 0; test < 14; ++test) {
            bool memory = test >= 9 && test <= 11;
            struct pf_state state = {.gpr = {original, memory ? BASE + 1 : value, 0, 0, value, 0, 0, 0, value},
                .rflags = flags, .memory = {bytes, BASE, sizeof(bytes), true}};
            extensions[test](&state);
            assert(pf_read_flags(&state) == flags && state.memory_fault == PF_MEM_OK);
#if defined(__x86_64__)
            uint64_t native = value;
#define EXTEND(op) __asm__ volatile(op : "+a"(native) : : "cc")
            switch (test) {
            case 0: case 12: case 13: EXTEND("movzbl %%al, %%eax"); break;
            case 1: EXTEND("movzbl %%ah, %%eax"); break;
            case 2: EXTEND("movzwl %%ax, %%eax"); break;
            case 3: EXTEND("movsbl %%al, %%eax"); break;
            case 4: EXTEND("movsbl %%ah, %%eax"); break;
            case 5: EXTEND("movswl %%ax, %%eax"); break;
            case 6: EXTEND("movsbq %%al, %%rax"); break;
            case 7: EXTEND("movswq %%ax, %%rax"); break;
            case 8: EXTEND("movslq %%eax, %%rax"); break;
            case 9: __asm__ volatile("movzbq (%1), %0" : "=r"(native) : "r"(bytes + 1) : "memory"); break;
            case 10: __asm__ volatile("movswq (%1), %0" : "=r"(native) : "r"(bytes + 1) : "memory"); break;
            default: __asm__ volatile("movslq (%1), %0" : "=r"(native) : "r"(bytes + 1) : "memory"); break;
            }
#undef EXTEND
            assert(state.gpr[0] == native);
#endif
            digest = pf_memory_digest(digest, state.gpr[0]);
        }
        uint8_t native_conditions[16];
#if defined(__x86_64__)
        __asm__ volatile("pushq %[flags]; popfq; "
            "seto 0(%[out]); setno 1(%[out]); setb 2(%[out]); setae 3(%[out]); "
            "sete 4(%[out]); setne 5(%[out]); setbe 6(%[out]); seta 7(%[out]); "
            "sets 8(%[out]); setns 9(%[out]); setp 10(%[out]); setnp 11(%[out]); "
            "setl 12(%[out]); setge 13(%[out]); setle 14(%[out]); setg 15(%[out])"
            : : [flags] "r"(flags | 2), [out] "r"(native_conditions) : "cc", "memory");
#else
        for (unsigned c = 0; c < 16; ++c) native_conditions[c] = pf_condition(flags, c);
#endif
        for (unsigned test = 0; test < 48; ++test) {
            struct pf_state state = {.gpr = {original, BASE + 1}, .rflags = flags, .memory = {bytes, BASE, sizeof(bytes), true}};
            conditions[test](&state);
            uint64_t want = native_conditions[test % 16];
            if (test < 16) assert(state.gpr[0] == ((original & ~UINT64_C(255)) | want));
            else if (test < 32) assert(state.gpr[0] == ((original & ~UINT64_C(0xff00)) | want << 8));
            else assert(bytes[1] == want && state.gpr[0] == original);
            assert(pf_read_flags(&state) == flags && state.memory_fault == PF_MEM_OK);
            digest = pf_memory_digest(digest, state.gpr[0]);
            if (test >= 32) digest = pf_memory_digest(digest, bytes[1]);
        }
        struct pf_state extended = {.gpr = {[8] = original}, .rflags = flags};
        pf_block_62(&extended);
        assert(extended.gpr[8] == ((original & ~UINT64_C(255)) | native_conditions[4]));
    }
    uint8_t bytes[2] = {0x12, 0x34};
    struct pf_state state = {.gpr = {UINT64_MAX, BASE + 1}, .rip = 0x100, .rflags = 0x8d5,
        .memory = {bytes, BASE, sizeof(bytes), false}};
    pf_block_10(&state);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.rip == 0x100 && state.gpr[0] == UINT64_MAX);
    state.memory_fault = PF_MEM_OK;
    pf_block_46(&state);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.rip == 0x100 && bytes[1] == 0x34);
    assert(pf_read_flags(&state) == 0x8d5);
    pf_memory_print_digest(digest);
}
