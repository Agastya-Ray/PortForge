#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define DECLARE(width) uint64_t pf_cbe_scalar##width(uint64_t, uint64_t); uint64_t pf_cbe_vector##width(uint64_t, uint64_t)
uint64_t pf_cbe_bit_slt(uint64_t, uint64_t);
DECLARE(8); DECLARE(16); DECLARE(32); DECLARE(64);
#define SIGNED_DECLARE(width) uint64_t pf_cbe_ashr##width(uint64_t, uint64_t); uint64_t pf_cbe_slt##width(uint64_t, uint64_t); uint64_t pf_cbe_sge##width(uint64_t, uint64_t)
SIGNED_DECLARE(8); SIGNED_DECLARE(16); SIGNED_DECLARE(32); SIGNED_DECLARE(64);
uint64_t pf_cbe_extend1(uint64_t, uint64_t); uint64_t pf_cbe_extend8(uint64_t, uint64_t);
uint64_t pf_cbe_extend16(uint64_t, uint64_t); uint64_t pf_cbe_extend32(uint64_t, uint64_t);
static uint64_t next(uint64_t *seed) { *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17; return *seed; }
int main(void) {
    const unsigned widths[] = {8, 16, 32, 64};
    uint64_t (*const scalar[])(uint64_t, uint64_t) = {pf_cbe_scalar8, pf_cbe_scalar16, pf_cbe_scalar32, pf_cbe_scalar64};
    uint64_t (*const vector[])(uint64_t, uint64_t) = {pf_cbe_vector8, pf_cbe_vector16, pf_cbe_vector32, pf_cbe_vector64};
    uint64_t (*const shifts[])(uint64_t, uint64_t) = {pf_cbe_ashr8, pf_cbe_ashr16, pf_cbe_ashr32, pf_cbe_ashr64};
    uint64_t (*const less[])(uint64_t, uint64_t) = {pf_cbe_slt8, pf_cbe_slt16, pf_cbe_slt32, pf_cbe_slt64};
    uint64_t (*const greater_equal[])(uint64_t, uint64_t) = {pf_cbe_sge8, pf_cbe_sge16, pf_cbe_sge32, pf_cbe_sge64};
    uint64_t (*const extensions[])(uint64_t, uint64_t) = {pf_cbe_extend8, pf_cbe_extend16, pf_cbe_extend32};
    uint64_t seed = UINT64_C(0xb28c45fa1267d309);
    for (unsigned type = 0; type < 4; ++type) {
        uint64_t mask = widths[type] == 64 ? UINT64_MAX : (UINT64_C(1) << widths[type]) - 1;
        for (unsigned i = 0; i < 1000000; ++i) {
            uint64_t a = next(&seed) & mask, b = next(&seed) & mask;
            if (i < 65536 && type == 0) { a = i >> 8; b = i & 255; }
            else {
                if (i % 16 == 0) a = 0;
                if (i % 16 == 1) b = mask;
                if (i % 16 == 2) b = a;
            }
            uint64_t want = a < b ? 0 : a - b;
            assert(scalar[type](a, b) == want);
            assert(vector[type](a, b) == want);
            uint64_t sign = UINT64_C(1) << (widths[type] - 1);
            unsigned count = (unsigned)(b & (widths[type] - 1));
            uint64_t shifted = a >> count;
            if (a & sign) shifted |= mask ^ (mask >> count);
            assert(shifts[type](a, count) == shifted);
            assert(less[type](a, b) == ((a ^ sign) < (b ^ sign)));
            assert(greater_equal[type](a, b) == ((a ^ sign) >= (b ^ sign)));
            if (type < 3) {
                uint64_t target_mask = widths[type] == 32 ? UINT64_MAX : (UINT64_C(1) << (2 * widths[type])) - 1;
                uint64_t extended = a | ((a & sign) ? target_mask ^ mask : 0);
                assert(extensions[type](a, 0) == extended);
            }
            assert(pf_cbe_bit_slt(a, b) == ((a & 1) > (b & 1)));
            assert(pf_cbe_extend1(a, 0) == ((a & 1) ? UINT64_MAX : 0));
        }
    }
    puts("LLVM-CBE scalar/vector unsigned saturation and signed lanes passed");
}
