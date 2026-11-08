#include <stdint.h>

#ifndef PF_X87_H
#include "../../../runtime/guest/x87.h"
#endif

#ifndef PF_MEMORY_H
#include "../../../runtime/guest/memory.h"
#endif

#if defined(__aarch64__) && !defined(PF_PORTABLE_ONLY)
#include <arm_neon.h>
#endif

struct pf_xmm {
    uint64_t qword[2];
};

_Static_assert(sizeof(struct pf_xmm) == 16, "XMM layout");

enum pf_packed_op { PF_PXOR, PF_PADDD, PF_PADDQ, PF_PSUBD, PF_PSUBQ };

enum pf_arithmetic { PF_NO_FLAGS, PF_ADD, PF_SUB };

struct pf_pending {
    uint64_t lhs, rhs, result;
    unsigned width;
    enum pf_arithmetic operation;
};

struct pf_state {
    uint64_t gpr[16];
    struct pf_xmm xmm[16];
    struct pf_x87_state x87;
    bool x87_unsupported;
    struct pf_memory memory;
    enum pf_memory_fault memory_fault;
    uint64_t fault_address;
    uint64_t rip, rflags;
    struct pf_pending pending;
};

static inline bool pf_guest_span(struct pf_state *state, uint64_t address, size_t width,
                                 size_t alignment, bool write, uint8_t **out) {
    enum pf_memory_fault fault = pf_memory_span(&state->memory, address, width, alignment, write, out);
    if (fault == PF_MEM_OK) return true;
    state->memory_fault = fault;
    state->fault_address = address;
    return false;
}

static inline bool pf_load_xmm(struct pf_state *state, uint64_t address, size_t alignment,
                               struct pf_xmm *value) {
    uint8_t *bytes;
    if (!pf_guest_span(state, address, 16, alignment, false, &bytes)) return false;
    *value = (struct pf_xmm){{pf_read_le64(bytes), pf_read_le64(bytes + 8)}};
    return true;
}

static inline bool pf_store_xmm(struct pf_state *state, uint64_t address, size_t alignment,
                                struct pf_xmm value) {
    uint8_t *bytes;
    if (!pf_guest_span(state, address, 16, alignment, true, &bytes)) return false;
    pf_write_le64(bytes, value.qword[0]);
    pf_write_le64(bytes + 8, value.qword[1]);
    return true;
}

static inline bool pf_load_x87_real(struct pf_state *state, uint64_t address, unsigned width) {
    if (!pf_x87_available(&state->x87)) {
        state->x87_unsupported = true;
        return false;
    }
    uint8_t *bytes;
    if (!pf_guest_span(state, address, width / 8, 1, false, &bytes)) return false;
    uint64_t bits = width == 32 ? pf_read_le32(bytes) : pf_read_le64(bytes);
    if (pf_x87_load_real(&state->x87, bits, width)) return true;
    state->x87_unsupported = true;
    return false;
}

#define PF_ARITHMETIC_FLAGS UINT64_C(0x8d5)

uint64_t pf_read_flags(const struct pf_state *state) {
    const struct pf_pending *flags = &state->pending;
    if (flags->operation == PF_NO_FLAGS) {
        return state->rflags;
    }

    uint64_t lhs = flags->lhs;
    uint64_t rhs = flags->rhs;
    uint64_t result = flags->result;
    uint64_t sign = UINT64_C(1) << (flags->width - 1);
    unsigned carry, overflow;
    if (flags->operation == PF_ADD) {
        carry = result < lhs;
        overflow = (~(lhs ^ rhs) & (lhs ^ result) & sign) != 0;
    } else {
        carry = lhs < rhs;
        overflow = ((lhs ^ rhs) & (lhs ^ result) & sign) != 0;
    }
    unsigned low = (unsigned)(result & 255);
    low ^= low >> 4;
    low ^= low >> 2;
    low ^= low >> 1;
    return (state->rflags & ~PF_ARITHMETIC_FLAGS)
        | carry
        | ((uint64_t)((low & 1) == 0) << 2)
        | ((lhs ^ rhs ^ result) & UINT64_C(0x10))
        | ((uint64_t)(result == 0) << 6)
        | ((uint64_t)((result & sign) != 0) << 7)
        | ((uint64_t)overflow << 11);
}


static inline struct pf_xmm pf_packed(enum pf_packed_op op, struct pf_xmm a, struct pf_xmm b) {
    struct pf_xmm result = {0};
#if defined(__aarch64__) && !defined(PF_PORTABLE_ONLY)
    uint64x2_t lhs = vld1q_u64(a.qword);
    uint64x2_t rhs = vld1q_u64(b.qword);
    uint64x2_t value = vdupq_n_u64(0);
    switch (op) {
    case PF_PXOR: value = veorq_u64(lhs, rhs); break;
    case PF_PADDQ: value = vaddq_u64(lhs, rhs); break;
    case PF_PSUBQ: value = vsubq_u64(lhs, rhs); break;
    case PF_PADDD:
        value = vreinterpretq_u64_u32(vaddq_u32(vreinterpretq_u32_u64(lhs),
                                                vreinterpretq_u32_u64(rhs)));
        break;
    case PF_PSUBD:
        value = vreinterpretq_u64_u32(vsubq_u32(vreinterpretq_u32_u64(lhs),
                                                vreinterpretq_u32_u64(rhs)));
        break;
    }
    vst1q_u64(result.qword, value);
#else
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t lhs = a.qword[i];
        uint64_t rhs = b.qword[i];
        switch (op) {
        case PF_PXOR: result.qword[i] = lhs ^ rhs; break;
        case PF_PADDQ: result.qword[i] = lhs + rhs; break;
        case PF_PSUBQ: result.qword[i] = lhs - rhs; break;
        case PF_PADDD:
        case PF_PSUBD: {
            uint32_t low_a = (uint32_t)lhs;
            uint32_t low_b = (uint32_t)rhs;
            uint32_t high_a = (uint32_t)(lhs >> 32);
            uint32_t high_b = (uint32_t)(rhs >> 32);
            uint32_t low = op == PF_PADDD ? low_a + low_b : low_a - low_b;
            uint32_t high = op == PF_PADDD ? high_a + high_b : high_a - high_b;
            result.qword[i] = ((uint64_t)high << 32) | low;
            break;
        }
        }
    }
#endif
    return result;
}
