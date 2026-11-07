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
    PF_X87_C1 = 0x0200,
};

struct pf_x87_result {
    struct pf_ext80 value;
    uint16_t status;
};

/* Compute addition for canonical finite inputs. The caller handles traps and stack state.
 * False means an unsupported input or precision control, with result left unchanged. */
bool pf_x87_add(struct pf_ext80 a, struct pf_ext80 b, uint16_t control,
                struct pf_x87_result *result);

#endif
