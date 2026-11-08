#ifndef PF_X87_H
#define PF_X87_H

#include <stdbool.h>
#include <stdint.h>

struct pf_ext80 {
    uint64_t significand;
    uint16_t sign_exponent;
};

enum pf_x87_status {
    PF_X87_INVALID = 0x0001,
    PF_X87_DENORMAL = 0x0002,
    PF_X87_DIVZERO = 0x0004,
    PF_X87_OVERFLOW = 0x0008,
    PF_X87_UNDERFLOW = 0x0010,
    PF_X87_INEXACT = 0x0020,
    PF_X87_STACK_FAULT = 0x0040,
    PF_X87_ERROR_SUMMARY = 0x0080,
    PF_X87_C1 = 0x0200,
    PF_X87_TOP = 0x3800,
};

struct pf_x87_result {
    struct pf_ext80 value;
    uint16_t status;
};

/* Compute addition for canonical finite inputs. The caller handles traps and stack state.
 * False means an unsupported input or precision control, with result left unchanged. */
bool pf_x87_add(struct pf_ext80 a, struct pf_ext80 b, uint16_t control,
                struct pf_x87_result *result);

struct pf_x87_state {
    struct pf_ext80 slots[8];
    uint16_t control, status;
    uint8_t occupied;
};

enum pf_x87_op { PF_FLD1, PF_FLDZ, PF_FCHS, PF_FABS, PF_FADD, PF_FADDP };

static inline bool pf_x87_available(const struct pf_x87_state *state) {
    return (state->control & 0x3f) == 0x3f && !(state->status & PF_X87_ERROR_SUMMARY);
}

/* Execute the supported register operation with all exception masks set.
 * False leaves the x87 state unchanged. */
bool pf_x87_execute(struct pf_x87_state *state, enum pf_x87_op op,
                    unsigned destination, unsigned source);

/* Push a finite IEEE binary32 or binary64 encoding. False leaves state unchanged. */
bool pf_x87_load_real(struct pf_x87_state *state, uint64_t bits, unsigned width);

#endif
