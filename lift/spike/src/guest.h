#include <stdint.h>

enum pf_arithmetic { PF_NO_FLAGS, PF_ADD, PF_SUB };

struct pf_pending {
    uint64_t lhs, rhs, result;
    unsigned width;
    enum pf_arithmetic operation;
};

struct pf_state {
    uint64_t gpr[16];
    uint64_t rip, rflags;
    struct pf_pending pending;
};

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
