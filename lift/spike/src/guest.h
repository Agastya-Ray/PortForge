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

enum pf_packed_op { PF_PXOR, PF_PADDD, PF_PADDQ, PF_PSUBD, PF_PSUBQ, PF_PAND, PF_PANDN, PF_POR, PF_PCMPEQD, PF_PCMPGTD };

enum pf_arithmetic { PF_NO_FLAGS, PF_ADD, PF_SUB, PF_LOGIC, PF_INC, PF_DEC, PF_ADC, PF_SBB, PF_IMUL };

struct pf_pending {
    uint64_t lhs, rhs, result;
    unsigned width;
    enum pf_arithmetic operation;
};

enum pf_integer_fault { PF_INT_OK, PF_INT_DIVIDE_ZERO, PF_INT_DIVIDE_OVERFLOW };

struct pf_state {
    uint64_t gpr[16];
    struct pf_xmm xmm[16];
    struct pf_x87_state x87;
    bool x87_unsupported;
    struct pf_memory memory;
    enum pf_memory_fault memory_fault;
    uint64_t fault_address;
    enum pf_integer_fault integer_fault;
    uint64_t rip, rflags;
    struct pf_pending pending;
    bool pe_exited;
    uint32_t pe_exit_code;
};

static inline bool pf_guest_span(struct pf_state *state, uint64_t address, size_t width,
                                 size_t alignment, bool write, uint8_t **out) {
    enum pf_memory_fault fault = pf_memory_span(&state->memory, address, width, alignment, write, out);
    if (fault == PF_MEM_OK) return true;
    state->memory_fault = fault;
    state->fault_address = address;
    return false;
}

static inline bool pf_push(struct pf_state *state, uint64_t value) {
    uint64_t rsp = state->gpr[4] - 8;
    uint8_t *bytes;
    if (!pf_guest_span(state, rsp, 8, 1, true, &bytes)) return false;
    pf_write_le64(bytes, value);
    state->gpr[4] = rsp;
    return true;
}

static inline bool pf_pop(struct pf_state *state, unsigned destination) {
    uint8_t *bytes;
    if (!pf_guest_span(state, state->gpr[4], 8, 1, false, &bytes)) return false;
    uint64_t value = pf_read_le64(bytes);
    state->gpr[4] += 8;
    state->gpr[destination] = value;
    return true;
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

static inline bool pf_x87_extended_memory(struct pf_state *state, uint64_t address, bool store) {
    if (!pf_x87_available(&state->x87)) {
        state->x87_unsupported = true;
        return false;
    }
    uint8_t *bytes;
    if (!pf_guest_span(state, address, 10, 1, store, &bytes)) return false;
    struct pf_ext80 value;
    if (store) {
        if (pf_x87_store_extended_pop(&state->x87, &value)) {
            pf_write_le64(bytes, value.significand);
            bytes[8] = (uint8_t)value.sign_exponent;
            bytes[9] = (uint8_t)(value.sign_exponent >> 8);
            return true;
        }
    } else {
        value = (struct pf_ext80){pf_read_le64(bytes), (uint16_t)(bytes[8] | ((uint16_t)bytes[9] << 8))};
        if (pf_x87_load_extended(&state->x87, value)) return true;
    }
    state->x87_unsupported = true;
    return false;
}

static inline bool pf_load_x87_integer(struct pf_state *state, uint64_t address, unsigned width) {
    if (!pf_x87_available(&state->x87)) {
        state->x87_unsupported = true;
        return false;
    }
    uint8_t *bytes;
    if (!pf_guest_span(state, address, width / 8, 1, false, &bytes)) return false;
    uint64_t bits = 0;
    for (unsigned i = 0; i < width / 8; ++i) bits |= (uint64_t)bytes[i] << (8 * i);
    if (pf_x87_load_integer(&state->x87, bits, width)) return true;
    state->x87_unsupported = true;
    return false;
}

uint64_t pf_read_flags(const struct pf_state *state) {
    const struct pf_pending *flags = &state->pending;
    if (flags->operation == PF_NO_FLAGS) {
        return state->rflags;
    }

    uint64_t lhs = flags->lhs;
    uint64_t rhs = flags->rhs;
    uint64_t result = flags->result;
    uint64_t sign = UINT64_C(1) << (flags->width - 1);
    unsigned carry = 0, overflow = 0;
    uint64_t auxiliary = state->rflags & UINT64_C(0x10);
    if (flags->operation == PF_IMUL) {
        uint64_t mask = flags->width == 64 ? UINT64_MAX : UINT32_MAX;
        bool negative = ((lhs ^ rhs) & sign) != 0;
        uint64_t a = (lhs & sign) ? (0 - lhs) & mask : lhs;
        uint64_t b = (rhs & sign) ? (0 - rhs) & mask : rhs;
        uint64_t limit = sign - !negative;
        bool truncated = b != 0 && a > limit / b;
        return (state->rflags & ~UINT64_C(0x801)) | (truncated ? UINT64_C(0x801) : 0);
    }
    if (flags->operation == PF_ADD || flags->operation == PF_INC || flags->operation == PF_ADC) {
        carry = result < lhs || (flags->operation == PF_ADC && (state->rflags & 1) && result == lhs);
        overflow = (~(lhs ^ rhs) & (lhs ^ result) & sign) != 0;
    } else if (flags->operation == PF_SUB || flags->operation == PF_DEC || flags->operation == PF_SBB) {
        carry = lhs < rhs || (flags->operation == PF_SBB && (state->rflags & 1) && lhs == rhs);
        overflow = ((lhs ^ rhs) & (lhs ^ result) & sign) != 0;
    }
    if (flags->operation == PF_INC || flags->operation == PF_DEC) carry = state->rflags & 1;
    if (flags->operation != PF_LOGIC) auxiliary = (lhs ^ rhs ^ result) & UINT64_C(0x10);
    unsigned low = (unsigned)(result & 255);
    low ^= low >> 4;
    low ^= low >> 2;
    low ^= low >> 1;
    return (state->rflags & ~PF_ARITHMETIC_FLAGS)
        | carry
        | ((uint64_t)((low & 1) == 0) << 2)
        | auxiliary
        | ((uint64_t)(result == 0) << 6)
        | ((uint64_t)((result & sign) != 0) << 7)
        | ((uint64_t)overflow << 11);
}


static inline bool pf_condition(uint64_t flags, unsigned condition) {
    bool carry = (flags & UINT64_C(0x001)) != 0;
    bool zero = (flags & UINT64_C(0x040)) != 0;
    bool sign = (flags & UINT64_C(0x080)) != 0;
    bool overflow = (flags & UINT64_C(0x800)) != 0;
    bool taken;
    switch (condition >> 1) {
    case 0: taken = overflow; break;
    case 1: taken = carry; break;
    case 2: taken = zero; break;
    case 3: taken = carry || zero; break;
    case 4: taken = sign; break;
    case 5: taken = (flags & UINT64_C(0x004)) != 0; break;
    case 6: taken = sign != overflow; break;
    default: taken = zero || sign != overflow; break;
    }
    return taken != ((condition & 1) != 0);
}

enum pf_scalar_shift { PF_SHL, PF_SHR, PF_SAR };

static inline void pf_shift_scalar(struct pf_state *state, unsigned reg, unsigned width,
                                    enum pf_scalar_shift op, uint64_t raw_count) {
    unsigned count = raw_count & (width - 1);
    uint64_t mask = width == 64 ? UINT64_MAX : UINT32_MAX;
    uint64_t input = state->gpr[reg] & mask;
    if (!count) {
        state->gpr[reg] = input;
        return;
    }
    uint64_t flags = pf_read_flags(state);
    uint64_t sign = UINT64_C(1) << (width - 1);
    unsigned carry;
    uint64_t result;
    if (op == PF_SHL) {
        carry = (input >> (width - count)) & 1;
        result = (input << count) & mask;
    } else {
        carry = (input >> (count - 1)) & 1;
        result = input >> count;
        if (op == PF_SAR && (input & sign)) result |= mask ^ (mask >> count);
    }
    unsigned parity = (unsigned)(result & 255);
    parity ^= parity >> 4;
    parity ^= parity >> 2;
    parity ^= parity >> 1;
    flags = (flags & ~UINT64_C(0xc5)) | carry | ((uint64_t)((parity & 1) == 0) << 2)
        | ((uint64_t)(result == 0) << 6) | ((uint64_t)((result & sign) != 0) << 7);
    if (count == 1) {
        unsigned overflow = op == PF_SHL ? ((result & sign) != 0) ^ carry
            : op == PF_SHR ? (input & sign) != 0 : 0;
        flags = (flags & ~UINT64_C(0x800)) | ((uint64_t)overflow << 11);
    }
    state->gpr[reg] = result;
    state->rflags = flags;
    state->pending.operation = PF_NO_FLAGS;
}

static inline struct pf_xmm pf_packed(enum pf_packed_op op, struct pf_xmm a, struct pf_xmm b) {
    struct pf_xmm result = {0};
#if defined(__aarch64__) && !defined(PF_PORTABLE_ONLY)
    uint64x2_t lhs = vld1q_u64(a.qword);
    uint64x2_t rhs = vld1q_u64(b.qword);
    uint64x2_t value = vdupq_n_u64(0);
    switch (op) {
    case PF_PXOR: value = veorq_u64(lhs, rhs); break;
    case PF_PAND: value = vandq_u64(lhs, rhs); break;
    case PF_PANDN: value = vbicq_u64(rhs, lhs); break;
    case PF_POR: value = vorrq_u64(lhs, rhs); break;
    case PF_PCMPEQD:
        value = vreinterpretq_u64_u32(vceqq_u32(vreinterpretq_u32_u64(lhs), vreinterpretq_u32_u64(rhs)));
        break;
    case PF_PCMPGTD:
        value = vreinterpretq_u64_u32(vcgtq_s32(vreinterpretq_s32_u64(lhs), vreinterpretq_s32_u64(rhs)));
        break;
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
        case PF_PAND: result.qword[i] = lhs & rhs; break;
        case PF_PANDN: result.qword[i] = ~lhs & rhs; break;
        case PF_POR: result.qword[i] = lhs | rhs; break;
        case PF_PADDQ: result.qword[i] = lhs + rhs; break;
        case PF_PSUBQ: result.qword[i] = lhs - rhs; break;
        case PF_PADDD:
        case PF_PSUBD:
        case PF_PCMPEQD:
        case PF_PCMPGTD: {
            uint32_t low_a = (uint32_t)lhs;
            uint32_t low_b = (uint32_t)rhs;
            uint32_t high_a = (uint32_t)(lhs >> 32);
            uint32_t high_b = (uint32_t)(rhs >> 32);
            uint32_t low, high;
            if (op == PF_PCMPEQD) {
                low = low_a == low_b ? UINT32_MAX : 0;
                high = high_a == high_b ? UINT32_MAX : 0;
            } else if (op == PF_PCMPGTD) {
                low = (low_a ^ UINT32_C(0x80000000)) > (low_b ^ UINT32_C(0x80000000)) ? UINT32_MAX : 0;
                high = (high_a ^ UINT32_C(0x80000000)) > (high_b ^ UINT32_C(0x80000000)) ? UINT32_MAX : 0;
            } else {
                low = op == PF_PADDD ? low_a + low_b : low_a - low_b;
                high = op == PF_PADDD ? high_a + high_b : high_a - high_b;
            }
            result.qword[i] = ((uint64_t)high << 32) | low;
            break;
        }
        }
    }
#endif
    return result;
}

enum pf_shift_op { PF_PSLLD, PF_PSLLQ, PF_PSRLD, PF_PSRLQ, PF_PSRAD };

static inline struct pf_xmm pf_xmm_shift(enum pf_shift_op op, struct pf_xmm input, unsigned count) {
    unsigned width = op == PF_PSLLQ || op == PF_PSRLQ ? 64 : 32;
    uint64_t mask = width == 64 ? UINT64_MAX : UINT32_MAX;
    struct pf_xmm result = {0};
    for (unsigned lane = 0; lane < 128 / width; ++lane) {
        unsigned index = lane * width / 64, shift = lane * width % 64;
        uint64_t value = (input.qword[index] >> shift) & mask;
        uint64_t bits = 0;
        if (op == PF_PSRAD) {
            unsigned amount = count < width ? count : width - 1;
            bits = value >> amount;
            if (value & (UINT64_C(1) << (width - 1))) bits |= mask ^ (mask >> amount);
        } else if (count < width) {
            bits = op == PF_PSLLD || op == PF_PSLLQ ? (value << count) & mask : value >> count;
        }
        result.qword[index] |= bits << shift;
    }
    return result;
}
