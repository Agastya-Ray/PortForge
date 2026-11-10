#include <assert.h>
#include <string.h>
#if defined(__x86_64__)
#include <emmintrin.h>
#endif
#include "lifted.c"
#include "memory-evidence.h"

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(void) {
    void (*const blocks[])(struct pf_state *) = {
        pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4, pf_block_5, pf_block_6,
        pf_block_7, pf_block_8, pf_block_9, pf_block_10, pf_block_11, pf_block_12, pf_block_13,
        pf_block_14, pf_block_15, pf_block_16, pf_block_17, pf_block_18, pf_block_19, pf_block_20,
        pf_block_21, pf_block_22, pf_block_23, pf_block_24, pf_block_25, pf_block_26, pf_block_27,
        pf_block_28, pf_block_29, pf_block_30, pf_block_31, pf_block_32, pf_block_33, pf_block_34,
    };
    const unsigned counts[] = {0, 1, 31, 32, 63, 64, 255};
    uint64_t seed = UINT64_C(0xa0f392b781cd65e4), digest = PF_MEMORY_DIGEST_SEED;
    for (unsigned i = 0; i < 1000000; ++i) {
        struct pf_xmm input = {{next(&seed), next(&seed)}};
        if (i % 32 == 0) input = (struct pf_xmm){{UINT64_MAX, UINT64_C(0x800000007fffffff)}};
        for (unsigned test = 0; test < 35; ++test) {
            struct pf_state state = {.gpr = {1, 2, 3}, .xmm = {[0] = input, [1] = input}, .rip = 0x100, .rflags = 0x8d5};
            blocks[test](&state);
            assert(state.rip == 0x105 && state.rflags == 0x8d5);
            assert(state.gpr[0] == 1 && state.gpr[1] == 2 && state.gpr[2] == 3);
            assert(memcmp(&state.xmm[1], &input, sizeof(input)) == 0);
#if defined(__x86_64__)
            __m128i value, result;
            memcpy(&value, &input, sizeof(value));
            __m128i count = _mm_cvtsi64_si128(counts[test % 7]);
            switch (test / 7) {
            case 0: result = _mm_sll_epi32(value, count); break;
            case 1: result = _mm_sll_epi64(value, count); break;
            case 2: result = _mm_srl_epi32(value, count); break;
            case 3: result = _mm_srl_epi64(value, count); break;
            default: result = _mm_sra_epi32(value, count); break;
            }
            assert(memcmp(&state.xmm[0], &result, sizeof(result)) == 0);
#else
            (void)counts;
#endif
            digest = pf_memory_digest(digest, state.xmm[0].qword[0]);
            digest = pf_memory_digest(digest, state.xmm[0].qword[1]);
        }
    }
    pf_memory_print_digest(digest);
}
