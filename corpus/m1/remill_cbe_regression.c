#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define DECLARE(width) uint64_t pf_cbe_scalar##width(uint64_t, uint64_t); uint64_t pf_cbe_vector##width(uint64_t, uint64_t)
DECLARE(8); DECLARE(16); DECLARE(32); DECLARE(64);
static uint64_t next(uint64_t *seed) { *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17; return *seed; }
int main(void) {
    const unsigned widths[] = {8, 16, 32, 64};
    uint64_t (*const scalar[])(uint64_t, uint64_t) = {pf_cbe_scalar8, pf_cbe_scalar16, pf_cbe_scalar32, pf_cbe_scalar64};
    uint64_t (*const vector[])(uint64_t, uint64_t) = {pf_cbe_vector8, pf_cbe_vector16, pf_cbe_vector32, pf_cbe_vector64};
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
        }
    }
    puts("LLVM-CBE unsigned saturation: scalar/vector 8/16/32/64 passed");
}
