#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lifted.c"

#define X87_OBSERVED UINT16_C(0x3aff)
#define INTEGER_BIT UINT64_C(0x8000000000000000)

typedef void (*block_fn)(struct pf_state *);
static const block_fn blocks[] = {
    pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4, pf_block_5, pf_block_6,
    pf_block_7, pf_block_8,
    pf_block_11, pf_block_12, pf_block_13, pf_block_14, pf_block_15, pf_block_16,
    pf_block_17, pf_block_18, pf_block_19,
};
static const char *const names[] = {
    "fld1", "fldz", "fchs", "fabs", "fadd-st0", "fadd-st1", "faddp-st1", "fadd-st3", "faddp-st0",
    "fsub-st0", "fsub-st1", "fsubp-st1", "fsubr-st0", "fsubr-st1", "fsubrp-st1",
    "fmul-st0", "fmul-st1", "fmulp-st1",
};

#if defined(__x86_64__)
#define NATIVE_X86 "true"
static struct pf_x87_state native_step(unsigned operation, uint16_t control,
                                       const struct pf_ext80 *values, unsigned depth) {
    __asm__ volatile("fninit; fldcw %0" : : "m"(control));
    for (unsigned i = 0; i < depth; ++i) {
        __asm__ volatile("fldt %0" : : "m"(values[i]) : "st");
    }
    switch (operation) {
    case 0: __asm__ volatile("fld1" : : : "st"); break;
    case 1: __asm__ volatile("fldz" : : : "st"); break;
    case 2: __asm__ volatile("fchs" : : : "st"); break;
    case 3: __asm__ volatile("fabs" : : : "st"); break;
    case 4: __asm__ volatile(".byte 0xd8, 0xc1" : : : "st", "st(1)"); break;
    case 5: __asm__ volatile(".byte 0xdc, 0xc1" : : : "st", "st(1)"); break;
    case 6: __asm__ volatile(".byte 0xde, 0xc1" : : : "st", "st(1)"); break;
    case 7: __asm__ volatile(".byte 0xd8, 0xc3" : : : "st", "st(3)"); break;
    case 8: __asm__ volatile(".byte 0xde, 0xc0" : : : "st"); break;
    case 9: __asm__ volatile(".byte 0xd8, 0xe1" : : : "st", "st(1)"); break;
    case 10: __asm__ volatile(".byte 0xdc, 0xe9" : : : "st", "st(1)"); break;
    case 11: __asm__ volatile(".byte 0xde, 0xe9" : : : "st", "st(1)"); break;
    case 12: __asm__ volatile(".byte 0xd8, 0xe9" : : : "st", "st(1)"); break;
    case 13: __asm__ volatile(".byte 0xdc, 0xe1" : : : "st", "st(1)"); break;
    case 14: __asm__ volatile(".byte 0xde, 0xe1" : : : "st", "st(1)"); break;
    case 15: __asm__ volatile(".byte 0xd8, 0xc9" : : : "st", "st(1)"); break;
    case 16: __asm__ volatile(".byte 0xdc, 0xc9" : : : "st", "st(1)"); break;
    case 17: __asm__ volatile(".byte 0xde, 0xc9" : : : "st", "st(1)"); break;
    default: abort();
    }
    _Alignas(16) unsigned char snapshot[512];
    __asm__ volatile("fxsave %0; fninit" : "=m"(snapshot));
    struct pf_x87_state result = {0};
    memcpy(&result.control, snapshot, 2);
    memcpy(&result.status, snapshot + 2, 2);
    result.occupied = snapshot[4];
    unsigned top = (result.status & PF_X87_TOP) >> 11;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned slot = (top + i) & 7;
        if (result.occupied & (1u << slot)) {
            memcpy(&result.slots[slot].significand, snapshot + 32 + 16 * i, 8);
            memcpy(&result.slots[slot].sign_exponent, snapshot + 40 + 16 * i, 2);
        }
    }
    return result;
}

static bool equal_value(struct pf_ext80 a, struct pf_ext80 b) {
    return a.significand == b.significand && a.sign_exponent == b.sign_exponent;
}
#else
#define NATIVE_X86 "false"
#endif

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed >> 12;
    *seed ^= *seed << 25;
    *seed ^= *seed >> 27;
    return *seed * UINT64_C(2685821657736338717);
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static void reject(const char *name, unsigned iteration, const char *detail) {
    fprintf(stderr, "%s case=%u: %s\n", name, iteration, detail);
    exit(1);
}

static struct pf_ext80 operand(uint64_t *seed, unsigned variant) {
    uint64_t a = next(seed), b = next(seed);
    switch (variant % 16) {
    case 0: return (struct pf_ext80){0, (uint16_t)(a & 0x8000)};
    case 1: return (struct pf_ext80){b & (INTEGER_BIT - 1), (uint16_t)(a & 0x8000)};
    case 2: return (struct pf_ext80){UINT64_MAX, (uint16_t)((a & 0x8000) | 0x7ffe)};
    default: return (struct pf_ext80){b | INTEGER_BIT,
                                     (uint16_t)((a & 0x8000) | (1 + (a >> 16) % 0x7ffe))};
    }
}

static uint64_t verify(unsigned op, unsigned samples, uint64_t digest) {
    const unsigned precision[] = {0, 0x200, 0x300};
    uint64_t seed = UINT64_C(0x4a51d3986db6139b);
    for (unsigned i = 0; i < samples; ++i) {
        unsigned depth = (i / 12) % 9;
        uint16_t control = 0x7f | precision[i % 3] | (((i / 3) % 4) << 10);
        struct pf_ext80 values[8];
        uint64_t rip = next(&seed), flags = next(&seed);
        struct pf_state state = {.rip = rip, .rflags = flags, .x87 = {.control = control}};
        unsigned top = 0;
        for (unsigned j = 0; j < depth; ++j) {
            values[j] = operand(&seed, i + j);
            if (op >= 9 && j >= 1 && j + 1 == depth) {
                if (i % 64 == 0) values[j] = values[j - 1];
                if (i % 64 == 1) {
                    values[j] = values[j - 1];
                    values[j].significand ^= 1;
                }
            }
            top = (top + 7) & 7;
            state.x87.slots[top] = values[j];
            state.x87.occupied |= 1u << top;
        }
        state.x87.status = top << 11;
        blocks[op](&state);
        if (state.x87_unsupported || state.rip != rip + 2 || state.rflags != flags) {
            reject(names[op], i, "execution boundary or integer flags differ");
        }
#if defined(__x86_64__)
        struct pf_x87_state native = native_step(op, control, values, depth);
        if (state.x87.control != native.control || state.x87.occupied != native.occupied
            || (state.x87.status & X87_OBSERVED) != (native.status & X87_OBSERVED)) {
            fprintf(stderr, "depth=%u control=%04x status=%04x/%04x tags=%02x/%02x\n",
                    depth, control, state.x87.status, native.status, state.x87.occupied, native.occupied);
            reject(names[op], i, "native control, status or tags differ");
        }
#endif
        for (unsigned slot = 0; slot < 8; ++slot) {
            if (state.x87.occupied & (1u << slot)) {
#if defined(__x86_64__)
                if (!equal_value(state.x87.slots[slot], native.slots[slot])) {
                    reject(names[op], i, "native x87 register bits differ");
                }
#endif
                digest = mix(mix(digest, state.x87.slots[slot].significand),
                             state.x87.slots[slot].sign_exponent);
            }
        }
        digest = mix(mix(digest, state.x87.status & X87_OBSERVED), state.x87.occupied);
    }
    return digest;
}

static void verify_stop(void) {
    struct pf_state state = {.rip = 0x1000, .x87 = {.control = 0x037e}};
    state.gpr[0] = state.gpr[1] = 1;
    struct pf_x87_state before;
    memcpy(&before, &state.x87, sizeof(before));
    pf_block_9(&state);
    if (!state.x87_unsupported || state.rip != 0x1002 || state.gpr[0] != 2
        || memcmp(&before, &state.x87, sizeof(before))) {
        reject("mixed", 0, "unsupported x87 operation did not stop at its own RIP");
    }

    const uint16_t controls[] = {0x037f, 0x017f, 0x037f};
    const block_fn binary[] = {pf_block_4, pf_block_13, pf_block_16, pf_block_19};
    for (unsigned op = 0; op < sizeof(binary) / sizeof(binary[0]); ++op) {
        for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
            state = (struct pf_state){.rip = 0x1000, .x87 = {.control = controls[i], .occupied = 3}};
            state.x87.slots[0] = (struct pf_ext80){INTEGER_BIT, 0x3fff};
            state.x87.slots[1] = (struct pf_ext80){INTEGER_BIT, i == 0 ? 0x7fff : 0x3fff};
            if (i == 2) state.x87.status = PF_X87_ERROR_SUMMARY;
            memcpy(&before, &state.x87, sizeof(before));
            binary[op](&state);
            if (!state.x87_unsupported || state.rip != 0x1000
                || memcmp(&before, &state.x87, sizeof(before))) {
                reject("unsupported", i, "RIP or x87 state changed");
            }
        }
    }
}

int main(void) {
    const unsigned samples = 1000000;
    uint64_t digest = UINT64_C(14695981039346656037);
    verify_stop();
    struct pf_state mixed = {.x87 = {.control = 0x037f, .status = PF_X87_DENORMAL}};
    pf_block_10(&mixed);
    if (mixed.rip != 8 || mixed.x87_unsupported || mixed.x87.occupied != 0x80
        || mixed.x87.status != (0x3800 | PF_X87_DENORMAL)
        || mixed.x87.slots[7].significand != INTEGER_BIT
        || mixed.x87.slots[7].sign_exponent != 0xc000) {
        reject("push-add-pop-sign", 0, "mixed stack state or sticky flags differ");
    }
    for (unsigned op = 0; op < sizeof(blocks) / sizeof(blocks[0]); ++op) {
        digest = verify(op, samples, digest);
    }
    printf("{\"native_x86_oracle\":%s,\"samples_per_encoding\":%u,"
           "\"encodings\":%zu,\"mismatches\":0,\"digest\":\"%016" PRIx64 "\"}\n",
           NATIVE_X86, samples, sizeof(blocks) / sizeof(blocks[0]), digest);
    return 0;
}
