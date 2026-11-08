#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "lifted.c"

#define X87_OBSERVED UINT16_C(0x3aff)
#define INTEGER_BIT UINT64_C(0x8000000000000000)

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

#if defined(__x86_64__)
static void check_native(const struct pf_x87_state *state, const uint8_t *bytes,
                         unsigned width, unsigned depth) {
    __asm__ volatile("fninit; fldcw %0" : : "m"(state->control));
    for (unsigned i = 0; i < depth; ++i) {
        __asm__ volatile("fld1" : : : "st");
    }
    if (width == 32) {
        __asm__ volatile("flds (%0)" : : "r"(bytes) : "st", "memory");
    } else {
        __asm__ volatile("fldl (%0)" : : "r"(bytes) : "st", "memory");
    }
    _Alignas(16) uint8_t snapshot[512];
    __asm__ volatile("fxsave %0; fninit" : "=m"(snapshot));
    uint16_t status = (uint16_t)pf_read_le32(snapshot + 2);
    assert((state->status & X87_OBSERVED) == (status & X87_OBSERVED));
    assert(state->occupied == snapshot[4]);
    unsigned top = (status & PF_X87_TOP) >> 11;
    assert(state->slots[top].significand == pf_read_le64(snapshot + 32));
    assert(state->slots[top].sign_exponent == (uint16_t)pf_read_le32(snapshot + 40));
}
#endif

static uint64_t verify(unsigned width, uint64_t digest) {
    uint64_t seed = UINT64_C(0x4c3d2e5a128fe12a);
    uint64_t sign = UINT64_C(1) << (width - 1);
    unsigned fraction_width = width == 32 ? 23 : 52;
    uint64_t fraction = (UINT64_C(1) << fraction_width) - 1;
    uint64_t exponent = sign - 1 - fraction;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t bits = next(&seed);
        if (width == 32) bits = (uint32_t)bits;
        if ((bits & exponent) == exponent) bits ^= UINT64_C(1) << fraction_width;
        switch (i % 16) {
        case 0: bits &= sign; break;
        case 1: bits &= sign | fraction; break;
        case 2: bits = (bits & sign) | 1; break;
        case 3: bits = (bits & sign) | fraction; break;
        case 4: bits = (bits & sign) | (UINT64_C(1) << fraction_width); break;
        case 5: bits = (bits & sign) | (exponent - 1); break;
        }
        unsigned depth = (i / 16) % 9;
        unsigned control = 0x7f | (((i / 144) % 4) << 10) | (((i / 576) % 4) << 8);
        uint8_t bytes[9] = {0};
        pf_write_le64(bytes + 1, bits);
        struct pf_state state = {
            .memory = {bytes, 0x1000, 1 + width / 8, false},
            .rip = 0x80, .rflags = 0x8d5, .x87 = {.control = control},
        };
        state.gpr[0] = 0x1001;
        unsigned top = (8 - depth) & 7;
        state.x87.status = top << 11;
        for (unsigned j = 0; j < depth; ++j) {
            unsigned slot = 7 - j;
            state.x87.slots[slot] = (struct pf_ext80){INTEGER_BIT, 0x3fff};
            state.x87.occupied |= 1u << slot;
        }
        if (width == 32) pf_block_0(&state);
        else pf_block_1(&state);
        assert(!state.x87_unsupported && state.memory_fault == PF_MEM_OK);
        assert(state.rip == 0x82 && state.rflags == 0x8d5 && state.x87.control == control);
#if defined(__x86_64__)
        check_native(&state.x87, bytes + 1, width, depth);
#endif
        top = (state.x87.status & PF_X87_TOP) >> 11;
        digest = mix(digest, state.x87.status & X87_OBSERVED);
        digest = mix(digest, state.x87.occupied);
        digest = mix(digest, state.x87.slots[top].significand);
        digest = mix(digest, state.x87.slots[top].sign_exponent);
    }
    return digest;
}

static void check_stops(void) {
    uint8_t bytes[8];
    struct pf_state state = {
        .memory = {bytes, 0x1000, sizeof(bytes), false},
        .gpr = {0x1001}, .rip = 0x80, .x87 = {.control = 0x037f},
    };
    pf_block_2(&state);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == 0x1001);
    assert(state.rip == 0x82 && !state.x87_unsupported && state.x87.occupied == 0x80);
    assert(state.x87.slots[7].sign_exponent == 0x3fff && state.x87.status == 0x3800);

    const uint64_t nonfinite[] = {
        UINT64_C(0x7ff0000000000000), UINT64_C(0x7ff0000000000001),
        UINT64_C(0x7ff8000000000000), UINT64_C(0xff800000),
        UINT64_C(0x7f800001), UINT64_C(0x7fc00000),
    };
    for (unsigned i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
        pf_write_le64(bytes, nonfinite[i]);
        state = (struct pf_state){
            .memory = {bytes, 0x1000, sizeof(bytes), false},
            .gpr = {0x1000}, .rip = 0x80, .x87 = {.control = 0x037f, .status = PF_X87_C1},
        };
        if (i < 3) pf_block_1(&state);
        else pf_block_0(&state);
        assert(state.x87_unsupported && state.memory_fault == PF_MEM_OK && state.rip == 0x80);
        assert(state.x87.status == PF_X87_C1 && state.x87.occupied == 0);
    }

    state = (struct pf_state){.rip = 0x80, .x87 = {.control = 0x037e}};
    pf_block_1(&state);
    assert(state.x87_unsupported && state.memory_fault == PF_MEM_OK && state.rip == 0x80);
    state = (struct pf_state){
        .rip = 0x80, .x87 = {.control = 0x037f, .status = PF_X87_ERROR_SUMMARY},
    };
    pf_block_1(&state);
    assert(state.x87_unsupported && state.memory_fault == PF_MEM_OK && state.rip == 0x80);
}

int main(void) {
    uint64_t digest = verify(32, UINT64_C(14695981039346656037));
    digest = verify(64, digest);
    check_stops();
    printf("%016" PRIx64 "\n", digest);
}
