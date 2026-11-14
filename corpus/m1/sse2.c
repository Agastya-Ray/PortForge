#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PF_USE_REMILL
#define PF_REMILL_XMM
#include "remill_backend.h"
#define PF_BACKEND "remill"
#else
#include "lifted.c"
#define PF_BACKEND "pfir"
#endif

typedef void (*block_fn)(struct pf_state *);

struct vector_case {
    const char *name;
    enum pf_packed_op operation;
    block_fn blocks[3];
};

static const struct vector_case cases[] = {
    {"pxor", PF_PXOR, {pf_block_0, pf_block_1, pf_block_2}},
    {"paddd", PF_PADDD, {pf_block_3, pf_block_4, pf_block_5}},
    {"paddq", PF_PADDQ, {pf_block_6, pf_block_7, pf_block_8}},
    {"psubd", PF_PSUBD, {pf_block_9, pf_block_10, pf_block_11}},
    {"psubq", PF_PSUBQ, {pf_block_12, pf_block_13, pf_block_14}},
    {"pand", PF_PAND, {pf_block_16, pf_block_17, pf_block_18}},
    {"pandn", PF_PANDN, {pf_block_19, pf_block_20, pf_block_21}},
    {"por", PF_POR, {pf_block_22, pf_block_23, pf_block_24}},
    {"pcmpeqd", PF_PCMPEQD, {pf_block_25, pf_block_26, pf_block_27}},
    {"pcmpgtd", PF_PCMPGTD, {pf_block_28, pf_block_29, pf_block_30}},
    {"paddb", PF_PADDB, {pf_block_31, pf_block_32, pf_block_33}},
    {"paddw", PF_PADDW, {pf_block_34, pf_block_35, pf_block_36}},
    {"psubb", PF_PSUBB, {pf_block_37, pf_block_38, pf_block_39}},
    {"psubw", PF_PSUBW, {pf_block_40, pf_block_41, pf_block_42}},
    {"paddsb", PF_PADDSB, {pf_block_43, pf_block_44, pf_block_45}},
    {"paddsw", PF_PADDSW, {pf_block_46, pf_block_47, pf_block_48}},
    {"paddusb", PF_PADDUSB, {pf_block_49, pf_block_50, pf_block_51}},
    {"paddusw", PF_PADDUSW, {pf_block_52, pf_block_53, pf_block_54}},
    {"psubsb", PF_PSUBSB, {pf_block_55, pf_block_56, pf_block_57}},
    {"psubsw", PF_PSUBSW, {pf_block_58, pf_block_59, pf_block_60}},
    {"psubusb", PF_PSUBUSB, {pf_block_61, pf_block_62, pf_block_63}},
    {"psubusw", PF_PSUBUSW, {pf_block_64, pf_block_65, pf_block_66}},
    {"pmullw", PF_PMULLW, {pf_block_67, pf_block_68, pf_block_69}},
    {"pmulhw", PF_PMULHW, {pf_block_70, pf_block_71, pf_block_72}},
    {"pmulhuw", PF_PMULHUW, {pf_block_73, pf_block_74, pf_block_75}},
    {"pmuludq", PF_PMULUDQ, {pf_block_76, pf_block_77, pf_block_78}},
};

#if defined(__x86_64__)
#include <emmintrin.h>
#define NATIVE_X86 "true"
#define RUN_NATIVE(op) __asm__ volatile(op " %1, %0" : "+x"(lhs) : "x"(rhs))
static struct pf_xmm native_reference(enum pf_packed_op operation, struct pf_xmm a,
                                      struct pf_xmm b) {
    __m128i lhs, rhs;
    memcpy(&lhs, &a, sizeof(lhs));
    memcpy(&rhs, &b, sizeof(rhs));
    switch (operation) {
    case PF_PADDB: RUN_NATIVE("paddb"); break;
    case PF_PADDW: RUN_NATIVE("paddw"); break;
    case PF_PSUBB: RUN_NATIVE("psubb"); break;
    case PF_PSUBW: RUN_NATIVE("psubw"); break;
    case PF_PADDSB: RUN_NATIVE("paddsb"); break;
    case PF_PADDSW: RUN_NATIVE("paddsw"); break;
    case PF_PADDUSB: RUN_NATIVE("paddusb"); break;
    case PF_PADDUSW: RUN_NATIVE("paddusw"); break;
    case PF_PSUBSB: RUN_NATIVE("psubsb"); break;
    case PF_PSUBSW: RUN_NATIVE("psubsw"); break;
    case PF_PSUBUSB: RUN_NATIVE("psubusb"); break;
    case PF_PSUBUSW: RUN_NATIVE("psubusw"); break;
    case PF_PMULLW: RUN_NATIVE("pmullw"); break;
    case PF_PMULHW: RUN_NATIVE("pmulhw"); break;
    case PF_PMULHUW: RUN_NATIVE("pmulhuw"); break;
    case PF_PMULUDQ: RUN_NATIVE("pmuludq"); break;
    case PF_PXOR: RUN_NATIVE("pxor"); break;
    case PF_PAND: RUN_NATIVE("pand"); break;
    case PF_PANDN: RUN_NATIVE("pandn"); break;
    case PF_POR: RUN_NATIVE("por"); break;
    case PF_PCMPEQD: RUN_NATIVE("pcmpeqd"); break;
    case PF_PCMPGTD: RUN_NATIVE("pcmpgtd"); break;
    case PF_PADDD: RUN_NATIVE("paddd"); break;
    case PF_PADDQ: RUN_NATIVE("paddq"); break;
    case PF_PSUBD: RUN_NATIVE("psubd"); break;
    case PF_PSUBQ: RUN_NATIVE("psubq"); break;
    }
    memcpy(&a, &lhs, sizeof(a));
    return a;
}
#else
#define NATIVE_X86 "false"
#endif

static uint64_t random_u64(uint64_t *seed) {
    *seed ^= *seed >> 12;
    *seed ^= *seed << 25;
    *seed ^= *seed >> 27;
    return *seed * UINT64_C(2685821657736338717);
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static void require(int condition, const char *operation, unsigned encoding,
                    unsigned iteration, const char *reason) {
    if (!condition) {
        fprintf(stderr, "%s encoding=%u case=%u: %s\n", operation, encoding, iteration, reason);
        exit(1);
    }
}

static uint64_t verify_case(const struct vector_case *test, unsigned samples, uint64_t digest) {
    static const uint64_t edge[] = {
        0, 1, UINT32_MAX, UINT64_C(1) << 32, UINT64_C(0x7fffffff80000000),
        UINT64_C(0x800000007fffffff), UINT64_MAX,
        UINT64_C(0x7f807f807f807f80), UINT64_C(0x00ff01fe7fff8000),
        UINT64_C(0x7fff80007fff8000), UINT64_C(0xffff0001ffff0001),
    };
    const unsigned edge_count = sizeof(edge) / sizeof(edge[0]);
    uint64_t seed = UINT64_C(0xae8d11932bc57c4f);
    for (unsigned i = 0; i < samples; ++i) {
        struct pf_xmm a, b;
        for (unsigned lane = 0; lane < 2; ++lane) {
            a.qword[lane] = random_u64(&seed);
            b.qword[lane] = random_u64(&seed);
        }
        if (i < edge_count * edge_count) {
            a = (struct pf_xmm){{edge[i / edge_count], edge[i % edge_count]}};
            b = (struct pf_xmm){{edge[i % edge_count], edge[i / edge_count]}};
        }
        uint64_t rip = random_u64(&seed);
        uint64_t rflags = random_u64(&seed);
        struct pf_xmm first = {0};
        for (unsigned encoding = 0; encoding < 3; ++encoding) {
            unsigned destination = encoding == 1 ? 8 : 0;
            unsigned source = encoding == 2 ? destination : destination + 1;
            struct pf_state state = {.rip = rip, .rflags = rflags};
            for (unsigned reg = 0; reg < 16; ++reg) {
                state.gpr[reg] = UINT64_C(0xb551041a63f29a30) ^ reg;
                state.xmm[reg] = (struct pf_xmm){{reg, ~((uint64_t)reg)}};
            }
            state.xmm[destination] = a;
            if (source != destination) {
                state.xmm[source] = b;
            }
            if (i & 1) state.pending = (struct pf_pending){UINT64_MAX, 1, 0, 64, PF_ADD};
            struct pf_state initial = state;
            test->blocks[encoding](&state);
            struct pf_xmm actual = state.xmm[destination];
#if defined(__x86_64__)
            struct pf_xmm native = native_reference(test->operation, a, initial.xmm[source]);
            require(memcmp(&actual, &native, sizeof(actual)) == 0,
                    test->name, encoding, i, "native x86 reference differs");
#endif
            if (encoding == 0) {
                first = actual;
            } else if (encoding == 1) {
                require(memcmp(&actual, &first, sizeof(actual)) == 0,
                        test->name, encoding, i, "extended register result differs");
            }
            initial.xmm[destination] = actual;
            require(memcmp(initial.xmm, state.xmm, sizeof(state.xmm)) == 0,
                    test->name, encoding, i, "unrelated XMM changed");
            require(memcmp(initial.gpr, state.gpr, sizeof(state.gpr)) == 0,
                    test->name, encoding, i, "general register changed");
            require(state.rflags == rflags && pf_read_flags(&state) == pf_read_flags(&initial)
                        && memcmp(&state.pending, &initial.pending, sizeof(state.pending)) == 0,
                    test->name, encoding, i, "flags changed");
            require(state.rip == rip + (encoding == 1 ? 5 : 4),
                    test->name, encoding, i, "RIP differs");
            digest = mix(mix(mix(digest, actual.qword[0]), actual.qword[1]), state.rip);
        }
    }
    return digest;
}

static void verify_mixed_block(void) {
    struct pf_state state = {.rip = UINT64_MAX - 1, .rflags = UINT64_C(0x202)};
    state.gpr[0] = UINT32_MAX;
    state.gpr[1] = 1;
    state.xmm[0] = (struct pf_xmm){{0x11, 0x22}};
    state.xmm[1] = (struct pf_xmm){{0x33, 0x44}};
    pf_block_15(&state);
    require(state.gpr[0] == 0 && state.rip == 4 && pf_read_flags(&state) == 0x257,
            "mixed", 0, 0, "ADD flags, result or block RIP differ");
    require(state.xmm[0].qword[0] == 0x22 && state.xmm[0].qword[1] == 0x66,
            "mixed", 0, 0, "PXOR result differs");
}

int main(void) {
    const unsigned samples = 1000000;
    uint64_t digest = UINT64_C(14695981039346656037);
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        digest = verify_case(&cases[index], samples, digest);
    }
    verify_mixed_block();
    printf("{\"backend\":\"%s\",\"native_x86_oracle\":%s,\"samples_per_operation\":%u,"
           "\"operations\":%zu,\"encodings_per_operation\":3,\"mismatches\":0,"
           "\"digest\":\"%016" PRIx64 "\"}\n", PF_BACKEND, NATIVE_X86, samples, sizeof(cases) / sizeof(cases[0]), digest);
    return 0;
}
