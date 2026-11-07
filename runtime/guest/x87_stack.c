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

bool pf_x87_execute(struct pf_x87_state *state, enum pf_x87_op op,
                    unsigned destination, unsigned source) {
    if ((state->control & 0x3f) != 0x3f || (state->status & PF_X87_ERROR_SUMMARY)) {
        return false;
    }

    unsigned top = (state->status & PF_X87_TOP) >> 11;
    switch (op) {
    case PF_FLD1:
    case PF_FLDZ: {
        unsigned slot = (top + 7) & 7;
        if (occupied(state, slot)) {
            stack_fault(state, slot, true);
        } else {
            struct pf_ext80 value = op == PF_FLD1
                ? (struct pf_ext80){UINT64_C(0x8000000000000000), 0x3fff}
                : (struct pf_ext80){0, 0};
            write_slot(state, slot, value);
            state->status &= ~PF_X87_C1;
        }
        set_top(state, slot);
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
    case PF_FADDP: {
        if (destination >= 8 || source >= 8
            || (op == PF_FADDP ? source != 0 : (source != 0 && destination != 0))) {
            return false;
        }
        unsigned lhs = (top + destination) & 7;
        unsigned rhs = (top + source) & 7;
        if (!occupied(state, lhs) || !occupied(state, rhs)) {
            stack_fault(state, lhs, false);
        } else {
            struct pf_x87_result result;
            if (!pf_x87_add(state->slots[lhs], state->slots[rhs], state->control, &result)) {
                return false;
            }
            write_slot(state, lhs, result.value);
            state->status = (state->status & ~PF_X87_C1) | result.status;
        }
        if (op == PF_FADDP) {
            state->occupied &= ~(1u << top);
            set_top(state, (top + 1) & 7);
        }
        return true;
    }
    }
    return false;
}
