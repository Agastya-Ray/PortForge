#include "x87.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define LITTLEENDIAN 1
#define THREAD_LOCAL _Thread_local
#include "softfloat.h"

#define X87_OBSERVED UINT16_C(0x023f)
#define INTEGER_BIT UINT64_C(0x8000000000000000)

#if defined(__x86_64__)
#define NATIVE_X86 "true"
_Static_assert(offsetof(struct pf_ext80, sign_exponent) == 8, "x87 operand layout");

static struct pf_x87_result native_add(struct pf_ext80 a, struct pf_ext80 b, uint16_t control) {
    struct pf_x87_result result = {0};
    uint16_t saved;
    __asm__ volatile("fnstcw %0" : "=m"(saved));
    __asm__ volatile("fnclex; fldcw %0" : : "m"(control));
    __asm__ volatile("fldt %2; fldt %3; faddp; fnstsw %1; fstpt %0"
        : "=m"(result.value), "=m"(result.status) : "m"(a), "m"(b) : "st", "st(1)");
    __asm__ volatile("fnclex; fldcw %0" : : "m"(saved));
    result.status &= X87_OBSERVED;
    return result;
}
#else
#define NATIVE_X86 "false"
#endif

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed >> 12;
    *seed ^= *seed << 25;
    *seed ^= *seed >> 27;
    return *seed * UINT64_C(2685821657736338717);
}

static struct pf_ext80 normal(uint64_t *seed) {
    uint64_t bits = next(seed);
    return (struct pf_ext80){
        next(seed) | INTEGER_BIT,
        (uint16_t)((bits & 0x8000) | (1 + (bits >> 16) % 0x7ffe)),
    };
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static void verify_rejections_and_backend_state(void) {
    struct pf_ext80 one = {INTEGER_BIT, 0x3fff};
    struct pf_x87_result out = {{1, 2}, 3};
    const struct pf_ext80 unsupported[] = {
        {INTEGER_BIT, 0}, {1, 1}, {INTEGER_BIT, 0x7fff},
    };
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        if (pf_x87_add(one, unsupported[i], 0x37f, &out)) abort();
    }
    if (pf_x87_add(one, one, 0x17f, &out) || out.value.significand != 1
        || out.value.sign_exponent != 2 || out.status != 3) abort();

    extF80_roundingPrecision = 32;
    softfloat_roundingMode = softfloat_round_max;
    softfloat_detectTininess = softfloat_tininess_beforeRounding;
    softfloat_exceptionFlags = softfloat_flag_invalid;
    if (!pf_x87_add(one, one, 0x37f, &out)
        || extF80_roundingPrecision != 32 || softfloat_roundingMode != softfloat_round_max
        || softfloat_detectTininess != softfloat_tininess_beforeRounding
        || softfloat_exceptionFlags != softfloat_flag_invalid) abort();
}

int main(void) {
    const unsigned samples = 100000;
    const unsigned precision[] = {0, 0x200, 0x300};
    const struct pf_ext80 edge[] = {
        {0, 0}, {0, 0x8000}, {1, 0}, {UINT64_C(0x7fffffffffffffff), 0},
        {INTEGER_BIT, 1}, {INTEGER_BIT, 0x8001}, {INTEGER_BIT, 0x3fff},
        {INTEGER_BIT, 0xbfff}, {UINT64_MAX, 0x7ffe}, {UINT64_MAX, 0xfffe},
    };
    const unsigned edge_count = sizeof(edge) / sizeof(edge[0]);
    uint64_t digest = UINT64_C(14695981039346656037);
    verify_rejections_and_backend_state();
    for (unsigned pc = 0; pc < 3; ++pc) {
        for (unsigned rc = 0; rc < 4; ++rc) {
            uint16_t control = 0x7f | precision[pc] | (rc << 10);
            uint64_t seed = UINT64_C(0xae8d11932bc57c4f);
            for (unsigned i = 0; i < samples; ++i) {
                struct pf_ext80 a = normal(&seed);
                struct pf_ext80 b = normal(&seed);
                if (i < edge_count * edge_count) {
                    a = edge[i / edge_count];
                    b = edge[i % edge_count];
                } else if (i % 16 == 0) {
                    // Exercise cancellation with close magnitudes.
                    b = a;
                    b.sign_exponent ^= 0x8000;
                    b.significand ^= 1;
                }
                struct pf_x87_result actual;
                if (!pf_x87_add(a, b, control, &actual)) abort();
#if defined(__x86_64__)
                struct pf_x87_result native = native_add(a, b, control);
                if (actual.value.significand != native.value.significand
                    || actual.value.sign_exponent != native.value.sign_exponent
                    || actual.status != native.status) {
                    fprintf(stderr, "FADD cw=%04x case=%u a=%04x:%016" PRIx64
                            " b=%04x:%016" PRIx64 " got=%04x:%016" PRIx64
                            "/%04x expected=%04x:%016" PRIx64 "/%04x\n",
                            control, i, a.sign_exponent, a.significand, b.sign_exponent,
                            b.significand, actual.value.sign_exponent, actual.value.significand,
                            actual.status, native.value.sign_exponent, native.value.significand,
                            native.status);
                    return 1;
                }
#endif
                digest = mix(mix(mix(digest, actual.value.significand),
                                 actual.value.sign_exponent), actual.status);
            }
        }
    }
    printf("{\"native_x86_oracle\":%s,\"samples_per_control\":%u,"
           "\"control_combinations\":12,\"mismatches\":0,\"digest\":\"%016" PRIx64 "\"}\n",
           NATIVE_X86, samples, digest);
    return 0;
}
