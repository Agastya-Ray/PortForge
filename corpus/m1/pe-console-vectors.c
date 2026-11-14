#include "pe-console-api.h"
#include <emmintrin.h>

#if defined(_MSC_VER)
#define PF_VECTOR_ALIGN __declspec(align(16))
#else
#define PF_VECTOR_ALIGN __attribute__((aligned(16)))
#endif

PF_VECTOR_ALIGN static volatile uint64_t input_a[2] = {
    UINT64_C(0x7fff8000ff007f80), UINT64_C(0xffff00017ffe8001)};
PF_VECTOR_ALIGN static volatile uint64_t input_b[2] = {
    UINT64_C(0x0001ffff0101807f), UINT64_C(0x80007fff0001ffff)};

void pf_entry(void) {
    __m128i a = *(const volatile __m128i *)(const void *)input_a;
    __m128i b = *(const volatile __m128i *)(const void *)input_b;
    __m128i result[16];
    result[0] = _mm_add_epi8(a, b);
    result[1] = _mm_add_epi16(a, b);
    result[2] = _mm_sub_epi8(a, b);
    result[3] = _mm_sub_epi16(a, b);
    result[4] = _mm_adds_epi8(a, b);
    result[5] = _mm_adds_epi16(a, b);
    result[6] = _mm_adds_epu8(a, b);
    result[7] = _mm_adds_epu16(a, b);
    result[8] = _mm_subs_epi8(a, b);
    result[9] = _mm_subs_epi16(a, b);
    result[10] = _mm_subs_epu8(a, b);
    result[11] = _mm_subs_epu16(a, b);
    result[12] = _mm_mullo_epi16(a, b);
    result[13] = _mm_mulhi_epi16(a, b);
    result[14] = _mm_mulhi_epu16(a, b);
    result[15] = _mm_mul_epu32(a, b);
    uint32_t written;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), result, sizeof(result), &written, 0) || written != sizeof(result)) ExitProcess(86);
    ExitProcess(0);
}
