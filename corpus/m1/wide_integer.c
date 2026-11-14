#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "lifted.c"
#include "memory-evidence.h"
#if defined(__x86_64__)
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#define NATIVE_BYTES(raw) __asm__ volatile("pushq %[before]; popfq; .byte " raw "; pushfq; popq %[after]" \
    : "+a"(state->gpr[0]), "+d"(state->gpr[2]), "+c"(state->gpr[1]), "+r"(extended), [after] "=&r"(*flags) \
    : "D"(bytes), [before] "r"(*flags | 2) : "cc", "memory")
#endif
struct wide_case { void (*block)(struct pf_state *); const char *name; unsigned width, operation; int reg; unsigned shift, length; };
#include "wide-integer-cases.h"
typedef unsigned __int128 u128;
typedef __int128 i128;

static uint64_t next(uint64_t *seed) { *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17; return *seed; }
static uint64_t mask(unsigned width) { return width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1; }
static void write_width(struct pf_state *s, unsigned reg, unsigned width, uint64_t value) {
    s->gpr[reg] = width >= 32 ? value & mask(width) : (s->gpr[reg] & ~mask(width)) | (value & mask(width));
}
static i128 signed_value(uint64_t value, unsigned width) {
    uint64_t sign = UINT64_C(1) << (width - 1);
    return (i128)((value & mask(width)) ^ sign) - (i128)sign;
}

/* Independent compiler 128-bit arithmetic oracle; runtime uses unsigned limbs. */
static enum pf_integer_fault reference(const struct wide_case *t, struct pf_state *s, uint64_t memory) {
    unsigned width = t->width;
    uint64_t m = mask(width), sign = UINT64_C(1) << (width - 1);
    if (t->operation == 4) {
        uint64_t value = s->gpr[0] & mask((unsigned)t->reg);
        bool negative = (value & (UINT64_C(1) << (t->reg - 1))) != 0;
        if (t->shift) value = negative ? m : 0;
        else if (negative) value |= m ^ mask((unsigned)t->reg);
        write_width(s, t->shift ? 2 : 0, width, value);
        return PF_INT_OK;
    }
    uint64_t source = (t->reg < 0 ? memory : s->gpr[t->reg] >> t->shift) & m;
    uint64_t low = s->gpr[0] & m;
    uint64_t high = width == 8 ? (s->gpr[0] >> 8) & m : s->gpr[2] & m;
    uint64_t q, r;
    if (t->operation <= 1) {
        u128 product = t->operation ? (u128)(signed_value(low, width) * signed_value(source, width)) : (u128)low * source;
        q = (uint64_t)product & m; r = (uint64_t)(product >> width) & m;
        bool overflow = t->operation ? signed_value(q, width) != signed_value(low, width) * signed_value(source, width) : r != 0;
        s->rflags = (pf_read_flags(s) & ~UINT64_C(0x801)) | (overflow ? UINT64_C(0x801) : 0);
        s->pending.operation = PF_NO_FLAGS;
    } else {
        if (source == 0) return PF_INT_DIVIDE_ZERO;
        u128 raw = ((u128)high << width) | low;
        if (t->operation == 2) {
            u128 quotient = raw / source;
            if (quotient > m) return PF_INT_DIVIDE_OVERFLOW;
            q = (uint64_t)quotient; r = (uint64_t)(raw % source);
        } else {
            u128 all = width == 64 ? ~(u128)0 : ((u128)1 << (2 * width)) - 1;
            i128 dividend = high & sign ? -1 - (i128)((~raw) & all) : (i128)raw;
            i128 divisor = signed_value(source, width);
            if (width == 64 && raw == ((u128)1 << 127) && divisor == -1) return PF_INT_DIVIDE_OVERFLOW;
            i128 quotient = dividend / divisor;
            if (quotient < -(i128)sign || quotient > (i128)sign - 1) return PF_INT_DIVIDE_OVERFLOW;
            q = (uint64_t)quotient & m; r = (uint64_t)(dividend % divisor) & m;
        }
    }
    if (width == 8) write_width(s, 0, 16, q | (r << 8));
    else { write_width(s, 0, width, q); write_width(s, 2, width, r); }
    return PF_INT_OK;
}

static void faults(void) {
    uint8_t bytes[16] = {0};
    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
        const struct wide_case *t = cases + test;
        if (t->reg != -1 || t->operation == 4) continue;
        struct pf_state s = {.gpr = {123, 0, 456, 0, 0, 0, 0, 0x1000 + 17 - t->width / 8}, .rip = 0x5000,
            .memory = {bytes, 0x1000, sizeof(bytes), false}, .pending = {UINT64_MAX, 1, 0, 64, PF_ADD}};
        const struct pf_state old = s;
        t->block(&s);
        assert(s.memory_fault == PF_MEM_UNMAPPED && s.integer_fault == PF_INT_OK);
        assert(s.rip == old.rip && memcmp(s.gpr, old.gpr, sizeof(s.gpr)) == 0);
        assert(s.rflags == old.rflags && memcmp(&s.pending, &old.pending, sizeof(s.pending)) == 0);
    }
#if defined(__x86_64__)
    struct rlimit no_core = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
        const struct wide_case *t = cases + test;
        if ((t->operation != 2 && t->operation != 3) || t->reg != 1) continue;
        for (unsigned mode = 0; mode < 2; ++mode) {
            struct pf_state s = {.gpr = {0, mode ? 1 : 0, 1}, .rflags = 0x202};
            if (t->width == 8 && mode) s.gpr[0] = UINT64_C(0x100);
            if (t->operation == 3 && mode) { s.gpr[0] = mask(t->width) & (UINT64_C(1) << (t->width - 1)); s.gpr[2] = mask(t->width); s.gpr[1] = mask(t->width); if (t->width == 8) s.gpr[0] |= UINT64_C(0xff00); }
            struct pf_state expected = s;
            assert(reference(t, &expected, 0) != PF_INT_OK);
            pid_t child = fork(); assert(child >= 0);
            if (!child) { uint64_t flags = s.rflags; native(test, &s, bytes, &flags); _exit(0); }
            int status; assert(waitpid(child, &status, 0) == child);
            assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE);
        }
    }
#endif
}

int main(void) {
    faults();
    uint64_t seed = UINT64_C(0x57ca420189defb63), digest = PF_MEMORY_DIGEST_SEED, valid[4] = {0};
    const uint64_t edges[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, UINT32_MAX, UINT64_MAX, UINT64_C(0x8000000000000000)};
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), d = next(&seed), c = next(&seed), e = next(&seed), memory = next(&seed);
        if (i % 4 == 0) { a = edges[(i / 4) % 12]; d = edges[(i / 48) % 12]; c = edges[(i / 576) % 12]; memory = c; }
        for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
            const struct wide_case *t = cases + test;
            uint8_t bytes[8]; pf_write_le64(bytes, memory);
            struct pf_state s = {.gpr = {a, c, d, 0, 0, 0, 0, 0x1000, e}, .rflags = 0x202 | (next(&seed) & PF_ARITHMETIC_FLAGS), .rip = 0x5000,
                .memory = {bytes, 0x1000, sizeof(bytes), false}};
            if (t->operation >= 2 && t->operation <= 3 && (i % 4 == 1 || i % 4 == 3)) {
                if (t->operation == 2) { s.gpr[2] = 0; if (t->width == 8) s.gpr[0] &= 0xff; }
                else { s.gpr[2] = s.gpr[0] & (UINT64_C(1) << (t->width - 1)) ? UINT64_MAX : 0; if (t->width == 8) s.gpr[0] = (s.gpr[0] & ~UINT64_C(0xff00)) | ((s.gpr[2] & 0xff) << 8); }
            }
            if (i % 3 == 0) s.pending = (struct pf_pending){a, c, a + c, 64, PF_ADD};
            const struct pf_state initial = s;
            struct pf_state expected = s;
            enum pf_integer_fault want_fault = reference(t, &expected, memory);
            t->block(&s);
            if (s.integer_fault != want_fault || memcmp(s.gpr, expected.gpr, sizeof(s.gpr)) != 0 || pf_read_flags(&s) != pf_read_flags(&expected)
                || s.memory_fault != PF_MEM_OK || s.rip != initial.rip + (want_fault ? 0 : t->length)) {
                fprintf(stderr, "%s sample %u fault %u/%u, RIP %" PRIx64 ", A=%" PRIx64 "/%" PRIx64 ", D=%" PRIx64 "/%" PRIx64 "\n", t->name, i, s.integer_fault, want_fault, s.rip, s.gpr[0], expected.gpr[0], s.gpr[2], expected.gpr[2]); abort();
            }
            if (want_fault || t->operation >= 2) assert(s.rflags == initial.rflags && memcmp(&s.pending, &initial.pending, sizeof(s.pending)) == 0);
#if defined(__x86_64__)
            if (!want_fault) {
                struct pf_state hardware = initial; uint64_t flags = pf_read_flags(&initial);
                native(test, &hardware, bytes, &flags);
                assert(memcmp(hardware.gpr, expected.gpr, sizeof(hardware.gpr)) == 0);
                uint64_t affected = t->operation <= 1 ? UINT64_C(0x801) : (t->operation == 4 ? PF_ARITHMETIC_FLAGS : 0);
                assert((flags & affected) == (pf_read_flags(&s) & affected));
            }
#endif
            if (!want_fault && t->operation < 4) ++valid[t->operation];
            digest = pf_memory_digest(digest, s.gpr[0]); digest = pf_memory_digest(digest, s.gpr[2]);
            digest = pf_memory_digest(digest, pf_read_flags(&s)); digest = pf_memory_digest(digest, s.integer_fault);
        }
    }
    for (unsigned op = 0; op < 4; ++op) assert(valid[op] >= 1000000);
    fprintf(stderr, "valid cases MUL=%" PRIu64 " IMUL=%" PRIu64 " DIV=%" PRIu64 " IDIV=%" PRIu64 "\n", valid[0], valid[1], valid[2], valid[3]);
    pf_memory_print_digest(digest);
}
