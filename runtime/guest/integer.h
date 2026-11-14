#ifndef PF_INTEGER_H
#define PF_INTEGER_H

/* Included after guest.h; all arithmetic below is unsigned and width-bounded. */
struct pf_u128 { uint64_t lo, hi; };
enum pf_integer_op { PF_FULL_MUL, PF_FULL_IMUL, PF_FULL_DIV, PF_FULL_IDIV };

static inline uint64_t pf_integer_mask(unsigned width) {
    return width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
}

static inline void pf_integer_write(struct pf_state *state, unsigned reg, unsigned width, uint64_t value) {
    uint64_t mask = pf_integer_mask(width);
    state->gpr[reg] = width >= 32 ? value & mask : (state->gpr[reg] & ~mask) | (value & mask);
}

static inline struct pf_u128 pf_multiply_full(uint64_t a, uint64_t b) {
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t low = a0 * b0;
    uint64_t middle = a1 * b0 + (low >> 32);
    uint64_t high = middle >> 32;
    middle = (uint32_t)middle + a0 * b1;
    return (struct pf_u128){(middle << 32) | (uint32_t)low, a1 * b1 + high + (middle >> 32)};
}

static inline void pf_negate_pair(uint64_t *high, uint64_t *low, uint64_t mask) {
    *low = (0 - *low) & mask;
    *high = (~*high + (*low == 0)) & mask;
}

/* Precondition: high < divisor, hence the quotient fits in width bits. */
static inline void pf_divide_full(uint64_t high, uint64_t low, uint64_t divisor,
                                   unsigned width, uint64_t *quotient, uint64_t *remainder) {
    uint64_t q = 0, r = high;
    for (unsigned bit = width; bit != 0; --bit) {
        bool carried = (r >> 63) != 0;
        r = (r << 1) | ((low >> (bit - 1)) & 1);
        if (carried || r >= divisor) { r -= divisor; q |= UINT64_C(1) << (bit - 1); }
    }
    *quotient = q;
    *remainder = r;
}

static inline bool pf_integer_full(struct pf_state *state, enum pf_integer_op operation,
                                    unsigned width, uint64_t source) {
    uint64_t mask = pf_integer_mask(width), sign = UINT64_C(1) << (width - 1);
    uint64_t low = state->gpr[0] & mask;
    uint64_t high = width == 8 ? (state->gpr[0] >> 8) & mask : state->gpr[2] & mask;
    source &= mask;
    if (operation == PF_FULL_MUL || operation == PF_FULL_IMUL) {
        bool a_negative = operation == PF_FULL_IMUL && (low & sign) != 0;
        bool b_negative = operation == PF_FULL_IMUL && (source & sign) != 0;
        uint64_t a = a_negative ? (0 - low) & mask : low;
        uint64_t b = b_negative ? (0 - source) & mask : source;
        struct pf_u128 product = pf_multiply_full(a, b);
        if (a_negative != b_negative) pf_negate_pair(&product.hi, &product.lo, UINT64_MAX);
        low = product.lo & mask;
        high = width == 64 ? product.hi : (product.lo >> width) & mask;
        bool overflow = operation == PF_FULL_MUL ? high != 0 : high != ((low & sign) ? mask : 0);
        uint64_t flags = pf_read_flags(state);
        if (width == 8) pf_integer_write(state, 0, 16, low | (high << 8));
        else { pf_integer_write(state, 0, width, low); pf_integer_write(state, 2, width, high); }
        state->rflags = (flags & ~UINT64_C(0x801)) | (overflow ? UINT64_C(0x801) : 0);
        state->pending.operation = PF_NO_FLAGS;
        return true;
    }
    if (source == 0) { state->integer_fault = PF_INT_DIVIDE_ZERO; return false; }
    bool dividend_negative = operation == PF_FULL_IDIV && (high & sign) != 0;
    bool divisor_negative = operation == PF_FULL_IDIV && (source & sign) != 0;
    if (dividend_negative) pf_negate_pair(&high, &low, mask);
    if (divisor_negative) source = (0 - source) & mask;
    if (high >= source) { state->integer_fault = PF_INT_DIVIDE_OVERFLOW; return false; }
    uint64_t quotient, remainder;
    pf_divide_full(high, low, source, width, &quotient, &remainder);
    bool quotient_negative = dividend_negative != divisor_negative;
    if (operation == PF_FULL_IDIV && quotient > sign - !quotient_negative) {
        state->integer_fault = PF_INT_DIVIDE_OVERFLOW;
        return false;
    }
    if (quotient_negative) quotient = (0 - quotient) & mask;
    if (dividend_negative) remainder = (0 - remainder) & mask;
    if (width == 8) pf_integer_write(state, 0, 16, quotient | (remainder << 8));
    else { pf_integer_write(state, 0, width, quotient); pf_integer_write(state, 2, width, remainder); }
    return true;
}

static inline void pf_integer_extend(struct pf_state *state, unsigned from, unsigned to, bool high) {
    uint64_t value = state->gpr[0] & pf_integer_mask(from);
    bool negative = (value & (UINT64_C(1) << (from - 1))) != 0;
    if (high) value = negative ? pf_integer_mask(to) : 0;
    else if (negative) value |= pf_integer_mask(to) ^ pf_integer_mask(from);
    pf_integer_write(state, high ? 2 : 0, to, value);
}

#endif
