#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PF_USE_REMILL
#include "remill_backend.h"
#define PF_BACKEND "remill"
#else
#include "lifted.c"
#define PF_BACKEND "pfir"
#endif

typedef void (*block_fn)(struct pf_state *);

enum instruction { ADD, SUB, CMP };

struct result {
    uint64_t destination, flags;
};

struct arithmetic_case {
    const char *name;
    enum instruction instruction;
    unsigned width;
    block_fn blocks[3];
};

static const struct arithmetic_case cases[] = {
    {"add32", ADD, 32, {pf_block_0, pf_block_1, pf_block_2}},
    {"add64", ADD, 64, {pf_block_3, pf_block_4, pf_block_5}},
    {"sub32", SUB, 32, {pf_block_6, pf_block_7, pf_block_8}},
    {"sub64", SUB, 64, {pf_block_9, pf_block_10, pf_block_11}},
    {"cmp32", CMP, 32, {pf_block_12, pf_block_13, pf_block_14}},
    {"cmp64", CMP, 64, {pf_block_15, pf_block_16, pf_block_17}},
};

#if defined(__x86_64__)
#define NATIVE_X86 "true"
#define RUN_NATIVE(op, left, right) \
    __asm__ volatile(op " " right ", " left "; pushfq; popq %1" \
        : "+r"(a), "=r"(flags) : "r"(b) : "cc", "memory")

static struct result native_reference(enum instruction op, unsigned width, uint64_t a, uint64_t b) {
    uint64_t flags;
    if (width == 32) {
        switch (op) {
        case ADD: RUN_NATIVE("addl", "%k0", "%k2"); break;
        case SUB: RUN_NATIVE("subl", "%k0", "%k2"); break;
        case CMP: RUN_NATIVE("cmpl", "%k0", "%k2"); break;
        default: abort();
        }
    } else {
        switch (op) {
        case ADD: RUN_NATIVE("addq", "%0", "%2"); break;
        case SUB: RUN_NATIVE("subq", "%0", "%2"); break;
        case CMP: RUN_NATIVE("cmpq", "%0", "%2"); break;
        default: abort();
        }
    }
    return (struct result){a, flags & PF_ARITHMETIC_FLAGS};
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

static void require(int condition, const char *name, unsigned encoding, unsigned iteration,
                    uint64_t a, uint64_t b, const char *reason) {
    if (!condition) {
        fprintf(stderr, "%s encoding=%u case=%u lhs=%016" PRIx64 " rhs=%016" PRIx64 ": %s\n",
                name, encoding, iteration, a, b, reason);
        exit(1);
    }
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static uint64_t verify_case(const struct arithmetic_case *test, unsigned samples, uint64_t digest) {
    static const uint64_t edge[] = {
        0, 1, 15, 16, UINT64_C(0x7fffffff), UINT64_C(0x80000000),
        UINT64_C(0xffffffff), INT64_MAX, UINT64_C(1) << 63, UINT64_MAX,
    };
    const unsigned edge_count = sizeof(edge) / sizeof(edge[0]);
    uint64_t seed = UINT64_C(0x48273123b591df15);
    for (unsigned iteration = 0; iteration < samples; ++iteration) {
        uint64_t a = random_u64(&seed);
        uint64_t b = random_u64(&seed);
        if (iteration < edge_count * edge_count) {
            a = edge[iteration / edge_count];
            b = edge[iteration % edge_count];
        }
        uint64_t rip = random_u64(&seed);
        uint64_t rflags = random_u64(&seed);
        struct result baseline = {0, 0};
#if defined(__x86_64__)
        struct result native = native_reference(test->instruction, test->width, a, b);
#endif
        for (unsigned encoding = 0; encoding < 3; ++encoding) {
            unsigned destination = encoding == 2 ? 8 : 0;
            unsigned source = destination + 1;
            unsigned bytes = test->width == 64 || encoding == 2 ? 3 : 2;
            struct pf_state state = {0};
            for (unsigned index = 0; index < 16; ++index) {
                state.gpr[index] = UINT64_C(0x5aa5891213d30f02) ^ index;
            }
            state.gpr[destination] = a;
            state.gpr[source] = b;
            state.rip = rip;
            state.rflags = rflags;
            struct pf_state initial = state;
            test->blocks[encoding](&state);

            require(state.rip == rip + bytes, test->name, encoding, iteration, a, b, "RIP differs");
#ifndef PF_USE_REMILL
            require(state.rflags == rflags && state.pending.operation != PF_NO_FLAGS,
                    test->name, encoding, iteration, a, b, "flags were not left pending");
#endif
            uint64_t flags = pf_read_flags(&state);
            require((flags & ~PF_ARITHMETIC_FLAGS) == (rflags & ~PF_ARITHMETIC_FLAGS),
                    test->name, encoding, iteration, a, b, "unrelated flags changed");
            struct result actual = {state.gpr[destination], flags & PF_ARITHMETIC_FLAGS};
#if defined(__x86_64__)
            require(actual.destination == native.destination && actual.flags == native.flags,
                    test->name, encoding, iteration, a, b, "native x86 reference differs");
#endif
            if (encoding == 0) {
                baseline = actual;
            } else {
                require(actual.destination == baseline.destination && actual.flags == baseline.flags,
                        test->name, encoding, iteration, a, b, "operand encodings differ");
            }
            initial.gpr[destination] = actual.destination;
            require(memcmp(initial.gpr, state.gpr, sizeof(state.gpr)) == 0,
                    test->name, encoding, iteration, a, b, "unrelated register changed");
            digest = mix(mix(mix(digest, actual.destination), flags), state.rip);
        }
    }
    return digest;
}

static uint64_t verify_block(unsigned samples, uint64_t digest) {
    uint64_t seed = UINT64_C(0x7ca48d12911e0ee5);
    for (unsigned iteration = 0; iteration < samples; ++iteration) {
        uint64_t a = random_u64(&seed);
        uint64_t b = random_u64(&seed);
        struct pf_state state = {.rip = UINT64_MAX - 3, .rflags = UINT64_C(0x202)};
        state.gpr[0] = a;
        state.gpr[1] = b;
        pf_block_18(&state);
        uint64_t flags = pf_read_flags(&state) & PF_ARITHMETIC_FLAGS;
#if defined(__x86_64__)
        struct result first = native_reference(ADD, 64, a, b);
        struct result second = native_reference(SUB, 32, first.destination, b);
        struct result native = native_reference(CMP, 64, second.destination, b);
        require(state.gpr[0] == native.destination && flags == native.flags,
                "block", 0, iteration, a, b, "native x86 reference differs");
#endif
        require(state.rip == 4 && state.gpr[1] == b,
                "block", 0, iteration, a, b, "block RIP or source register differs");
        digest = mix(mix(digest, state.gpr[0]), flags);
    }
    return digest;
}

static int benchmark(void) {
    const unsigned samples = 1000000;
    uint64_t seed = UINT64_C(0x48273123b591df15);
    uint64_t digest = UINT64_C(14695981039346656037);
    clock_t start = clock();
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct pf_state state = {0};
        block_fn run = cases[index].blocks[0];
        for (unsigned iteration = 0; iteration < samples; ++iteration) {
            state.gpr[0] = random_u64(&seed);
            state.gpr[1] = random_u64(&seed);
            run(&state);
            digest = mix(mix(digest, state.gpr[0]), pf_read_flags(&state));
        }
    }
    clock_t stop = clock();
    if (start == (clock_t)-1 || stop == (clock_t)-1 || stop <= start) {
        fputs("unable to measure benchmark CPU time\n", stderr);
        return 1;
    }
    unsigned blocks = samples * (sizeof(cases) / sizeof(cases[0]));
    double ns = (double)(stop - start) * 1e9 / CLOCKS_PER_SEC / blocks;
    printf("{\"backend\":\"%s\",\"blocks\":%u,\"cpu_ns_per_block\":%.3f,"
           "\"digest\":\"%016" PRIx64 "\"}\n", PF_BACKEND, blocks, ns, digest);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) {
        return benchmark();
    }
    if (argc != 1) {
        fputs("unexpected argument\n", stderr);
        return 2;
    }

    const unsigned samples = 1000000;
    uint64_t digest = UINT64_C(14695981039346656037);
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        digest = verify_case(&cases[index], samples, digest);
    }
    digest = verify_block(samples, digest);
    printf("{\"backend\":\"%s\",\"native_x86_oracle\":%s,\"samples_per_instruction_width\":%u,"
           "\"instruction_width_pairs\":6,\"encodings_per_pair\":3,"
           "\"three_instruction_samples\":%u,\"mismatches\":0,"
           "\"digest\":\"%016" PRIx64 "\"}\n", PF_BACKEND, NATIVE_X86, samples, samples, digest);
    return 0;
}
