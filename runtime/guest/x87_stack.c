#include "x87.h"

static bool occupied(const struct pf_x87_state *state, unsigned slot) {
    return (state->occupied & (1u << slot)) != 0;
}

static void set_top(struct pf_x87_state *state, unsigned top) {
    state->status = (state->status & ~PF_X87_TOP) | (top << 11);
}

static void write_slot(struct pf_x87_state *state, unsigned slot, struct pf_ext80 value) {
    state->slots[slot] = value;
    state->occupied |= 1u << slot;
}

static void stack_fault(struct pf_x87_state *state, unsigned slot, bool overflow) {
    state->status &= ~PF_X87_C1;
    state->status |= PF_X87_INVALID | PF_X87_STACK_FAULT | (overflow ? PF_X87_C1 : 0);
    write_slot(state, slot, (struct pf_ext80){UINT64_C(0xc000000000000000), 0xffff});
}

static void push(struct pf_x87_state *state, struct pf_ext80 value, uint16_t flags) {
    unsigned top = (state->status & PF_X87_TOP) >> 11;
    unsigned slot = (top + 7) & 7;
    if (occupied(state, slot)) {
        stack_fault(state, slot, true);
    } else {
        write_slot(state, slot, value);
        state->status = (state->status & ~PF_X87_C1) | flags;
    }
    set_top(state, slot);
}

bool pf_x87_load_extended(struct pf_x87_state *state, struct pf_ext80 value) {
    unsigned exponent = value.sign_exponent & 0x7fff;
    if (!pf_x87_available(state) || exponent == 0x7fff || ((value.significand >> 63) != (exponent != 0))) return false;
    push(state, value, 0);
    return true;
}

bool pf_x87_store_extended_pop(struct pf_x87_state *state, struct pf_ext80 *value) {
    if (!pf_x87_available(state)) return false;
    unsigned top = (state->status & PF_X87_TOP) >> 11;
    state->status &= ~PF_X87_C1;
    if (occupied(state, top)) {
        *value = state->slots[top];
    } else {
        state->status |= PF_X87_INVALID | PF_X87_STACK_FAULT;
        *value = (struct pf_ext80){UINT64_C(0xc000000000000000), 0xffff};
    }
    state->occupied &= ~(1u << top);
    set_top(state, (top + 1) & 7);
    return true;
}

bool pf_x87_load_integer(struct pf_x87_state *state, uint64_t bits, unsigned width) {
    if (!pf_x87_available(state) || (width != 16 && width != 32 && width != 64)) return false;
    uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    bits &= mask;
    bool negative = (bits >> (width - 1)) != 0;
    uint64_t magnitude = negative ? (UINT64_C(0) - bits) & mask : bits;
    struct pf_ext80 value = {0, 0};
    if (magnitude) {
        unsigned power = 63;
        while (!(magnitude >> 63)) {
            magnitude <<= 1;
            --power;
        }
        value = (struct pf_ext80){magnitude, (negative ? 0x8000 : 0) | (16383 + power)};
    }
    push(state, value, 0);
    return true;
}

bool pf_x87_load_real(struct pf_x87_state *state, uint64_t bits, unsigned width) {
    if (!pf_x87_available(state) || (width != 32 && width != 64)) return false;

    unsigned fraction_width = width == 32 ? 23 : 52;
    unsigned bias = width == 32 ? 127 : 1023;
    unsigned exponent_mask = 2 * bias + 1;
    unsigned exponent = (bits >> fraction_width) & exponent_mask;
    if (exponent == exponent_mask) return false;

    uint16_t sign = ((bits >> (width - 1)) & 1) << 15;
    uint64_t integer_bit = UINT64_C(1) << fraction_width;
    uint64_t fraction = bits & (integer_bit - 1);
    if (!exponent && !fraction) {
        push(state, (struct pf_ext80){0, sign}, 0);
        return true;
    }

    int power = (int)exponent - (int)bias;
    uint16_t flags = 0;
    if (exponent) {
        fraction |= integer_bit;
    } else {
        ++power;
        while (!(fraction & integer_bit)) {
            fraction <<= 1;
            --power;
        }
        flags = PF_X87_DENORMAL;
    }
    struct pf_ext80 value = {fraction << (63 - fraction_width), sign | (power + 16383)};
    push(state, value, flags);
    return true;
}

bool pf_x87_execute(struct pf_x87_state *state, enum pf_x87_op op,
                    unsigned destination, unsigned source) {
    if (!pf_x87_available(state)) return false;

    unsigned top = (state->status & PF_X87_TOP) >> 11;
    switch (op) {
    case PF_FLD1:
    case PF_FLDZ: {
        struct pf_ext80 value = op == PF_FLD1
            ? (struct pf_ext80){UINT64_C(0x8000000000000000), 0x3fff}
            : (struct pf_ext80){0, 0};
        push(state, value, 0);
        return true;
    }
    case PF_FCHS:
    case PF_FABS:
        if (!occupied(state, top)) {
            stack_fault(state, top, false);
        } else {
            if (op == PF_FCHS) state->slots[top].sign_exponent ^= 0x8000;
            else state->slots[top].sign_exponent &= 0x7fff;
            state->status &= ~PF_X87_C1;
        }
        return true;
    case PF_FADD:
    case PF_FADDP:
    case PF_FSUB:
    case PF_FSUBP:
    case PF_FSUBR:
    case PF_FSUBRP:
    case PF_FMUL:
    case PF_FMULP: {
        bool pops = op == PF_FADDP || op == PF_FSUBP || op == PF_FSUBRP || op == PF_FMULP;
        if (destination >= 8 || source >= 8
            || (pops ? source != 0 : (source != 0 && destination != 0))) {
            return false;
        }
        unsigned lhs = (top + destination) & 7;
        unsigned rhs = (top + source) & 7;
        if (!occupied(state, lhs) || !occupied(state, rhs)) {
            stack_fault(state, lhs, false);
        } else {
            struct pf_x87_result result;
            struct pf_ext80 a = state->slots[lhs], b = state->slots[rhs];
            enum pf_x87_binary_op operation = PF_X87_SUBTRACT;
            if (op == PF_FADD || op == PF_FADDP) operation = PF_X87_ADD;
            if (op == PF_FMUL || op == PF_FMULP) operation = PF_X87_MULTIPLY;
            bool reversed = op == PF_FSUBR || op == PF_FSUBRP;
            if (!pf_x87_binary(operation, reversed ? b : a, reversed ? a : b, state->control, &result)) {
                return false;
            }
            write_slot(state, lhs, result.value);
            state->status = (state->status & ~PF_X87_C1) | result.status;
        }
        if (pops) {
            state->occupied &= ~(1u << top);
            set_top(state, (top + 1) & 7);
        }
        return true;
    }
    }
    return false;
}
