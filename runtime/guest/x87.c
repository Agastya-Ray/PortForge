#include "x87.h"

#define LITTLEENDIAN 1
#define THREAD_LOCAL _Thread_local
#include "softfloat.h"

static bool canonical_finite(struct pf_ext80 value) {
    unsigned exponent = value.sign_exponent & 0x7fff;
    bool integer_bit = (value.significand >> 63) != 0;
    return exponent != 0x7fff && integer_bit == (exponent != 0);
}

static bool subnormal(struct pf_ext80 value) {
    return (value.sign_exponent & 0x7fff) == 0 && value.significand != 0;
}

static bool greater_magnitude(extFloat80_t a, extFloat80_t b) {
    unsigned a_exp = a.signExp & 0x7fff;
    unsigned b_exp = b.signExp & 0x7fff;
    return a_exp > b_exp || (a_exp == b_exp && a.signif > b.signif);
}

bool pf_x87_add(struct pf_ext80 a, struct pf_ext80 b, uint16_t control,
                struct pf_x87_result *result) {
    static const unsigned precision[] = {32, 0, 64, 80};
    static const unsigned rounding[] = {
        softfloat_round_near_even, softfloat_round_min,
        softfloat_round_max, softfloat_round_minMag,
    };
    unsigned width = precision[(control >> 8) & 3];
    if (width == 0 || !canonical_finite(a) || !canonical_finite(b)) {
        return false;
    }

    uint_fast8_t saved_precision = extF80_roundingPrecision;
    uint_fast8_t saved_rounding = softfloat_roundingMode;
    uint_fast8_t saved_tininess = softfloat_detectTininess;
    uint_fast8_t saved_flags = softfloat_exceptionFlags;

    extF80_roundingPrecision = width;
    softfloat_roundingMode = rounding[(control >> 10) & 3];
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
    extFloat80_t lhs = {.signif = a.significand, .signExp = a.sign_exponent};
    extFloat80_t rhs = {.signif = b.significand, .signExp = b.sign_exponent};
    extFloat80_t value;
    extF80M_add(&lhs, &rhs, &value);
    uint_fast8_t flags = softfloat_exceptionFlags;
    uint16_t status = (subnormal(a) || subnormal(b)) ? PF_X87_DENORMAL : 0;
    if (flags & softfloat_flag_invalid) status |= PF_X87_INVALID;
    if (flags & softfloat_flag_infinite) status |= PF_X87_DIVZERO;
    if (flags & softfloat_flag_overflow) status |= PF_X87_OVERFLOW;
    if (flags & softfloat_flag_underflow) status |= PF_X87_UNDERFLOW;
    if (flags & softfloat_flag_inexact) {
        status |= PF_X87_INEXACT;
        if (softfloat_roundingMode != softfloat_round_minMag) {
            extFloat80_t truncated;
            softfloat_roundingMode = softfloat_round_minMag;
            extF80M_add(&lhs, &rhs, &truncated);
            if (greater_magnitude(value, truncated)) {
                status |= PF_X87_C1;
            }
        }
    }

    extF80_roundingPrecision = saved_precision;
    softfloat_roundingMode = saved_rounding;
    softfloat_detectTininess = saved_tininess;
    softfloat_exceptionFlags = saved_flags;

    *result = (struct pf_x87_result){{value.signif, value.signExp}, status};
    return true;
}
