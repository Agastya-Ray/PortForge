#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#ifdef __MSVCRT__
#include <fcntl.h>
#include <io.h>
#endif
#include "lifted.c"
#include "runtime/host/wine_imports.h"

#define BASE UINT64_C(0x140000000)
#define SIZE 0x10000
#define STOP UINT64_C(0x70001000)
#define FIRST_IMPORT UINT64_C(0x70000000)
#define FIRST_BUFFER 0x301
#define SECOND_BUFFER 0x311

static struct pf_state caller(uint8_t *memory, bool convert) {
    pf_write_le64(memory + SIZE - 8, STOP);
    return (struct pf_state){
        .rip = PF_PE_ENTRY,
        .gpr = {[1] = BASE + FIRST_BUFFER, [2] = BASE + SECOND_BUFFER,
                [4] = BASE + SIZE - 8, [8] = convert, [9] = 1},
        .memory = {memory, BASE, SIZE, true},
    };
}

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static uint64_t mix(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static FILETIME filetime(uint64_t bits) {
    return (FILETIME){(DWORD)bits, (DWORD)(bits >> 32)};
}

int main(void) {
#ifdef __MSVCRT__
    /* The evidence format is ASCII hex followed by one LF byte. */
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        perror("PortForge could not set binary evidence output");
        return 1;
    }
#endif
    struct pf_wine_api api;
    DWORD error = pf_wine_open(&api);
    if (error != ERROR_SUCCESS) {
        fprintf(stderr, "PortForge Wine export lookup failed: %lu\n", (unsigned long)error);
        return 1;
    }
    uint8_t memory[SIZE] = {0};
    struct pf_state state = caller(memory, false);
    struct pf_pe_binding bindings[2];
    assert(pf_pe_import_count == 2);
    assert(pf_pe_bind_imports(&state, api.exports, PF_WINE_EXPORT_COUNT, FIRST_IMPORT, bindings, 2).status == PF_PE_BIND_OK);

    uint64_t seed = UINT64_C(0xd1375c4ce8ed13ef);
    uint64_t digest = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 100000; ++i) {
        uint64_t a = next(&seed) % UINT64_C(4414818240000000000);
        uint64_t b = i % 3 == 0 ? a : next(&seed) % UINT64_C(4414818240000000000);
        FILETIME left = filetime(a), right = filetime(b);
        LONG comparison = CompareFileTime(&left, &right);
        pf_write_le64(memory + FIRST_BUFFER, a);
        pf_write_le64(memory + SECOND_BUFFER, b);
        state = caller(memory, false);
        assert(pf_pe_run_bound(&state, STOP, 16, bindings, 2) == PF_PE_RETURNED);
        assert(state.gpr[0] == (uint32_t)comparison && state.gpr[4] == BASE + SIZE);
        digest = mix(digest, state.gpr[0]);

        SYSTEMTIME native;
        _Static_assert(sizeof(native) == 16, "expected Win32 SYSTEMTIME size");
        BOOL ok = FileTimeToSystemTime(&left, &native);
        assert(ok);
        memset(memory + SECOND_BUFFER, 0xa5, sizeof(native));
        state = caller(memory, true);
        assert(pf_pe_run_bound(&state, STOP, 16, bindings, 2) == PF_PE_RETURNED);
        assert(state.gpr[0] == (uint32_t)ok && state.gpr[4] == BASE + SIZE);
        assert(memcmp(memory + SECOND_BUFFER, &native, sizeof(native)) == 0);
        digest = mix(digest, pf_read_le64(memory + SECOND_BUFFER));
        digest = mix(digest, pf_read_le64(memory + SECOND_BUFFER + 8));
    }
    FILETIME invalid = filetime(UINT64_MAX);
    SYSTEMTIME output;
    assert(!FileTimeToSystemTime(&invalid, &output));
    pf_write_le64(memory + FIRST_BUFFER, UINT64_MAX);
    memset(memory + SECOND_BUFFER, 0xa5, sizeof(output));
    state = caller(memory, true);
    assert(pf_pe_run_bound(&state, STOP, 16, bindings, 2) == PF_PE_RETURNED && !state.gpr[0]);
    for (size_t i = 0; i < sizeof(output); ++i) assert(memory[SECOND_BUFFER + i] == 0xa5);
    struct pf_wine_api other = {0};
    SetLastError(0x7654);
    pf_wine_set_error(&api, 0x1234, 0, 0, 0);
    assert(pf_wine_get_error(&api, 0, 0, 0, 0) == 0x1234);
    assert(pf_wine_get_error(&other, 0, 0, 0, 0) == 0);
    assert(pf_wine_convert(&api, (uintptr_t)(memory + FIRST_BUFFER),
                           (uintptr_t)(memory + SECOND_BUFFER), 0, 0) == FALSE);
    assert(GetLastError() == 0x7654);
    uint8_t clock_bytes[9] = {0};
    SetLastError(0x4321);
    assert(pf_wine_frequency(&api, (uintptr_t)(clock_bytes + 1), 0, 0, 0));
    assert(pf_read_le64(clock_bytes + 1) > 0 && GetLastError() == 0x4321);
    assert(pf_wine_counter(&api, (uintptr_t)(clock_bytes + 1), 0, 0, 0));
    uint64_t first = pf_read_le64(clock_bytes + 1);
    assert(pf_wine_counter(&api, (uintptr_t)(clock_bytes + 1), 0, 0, 0));
    assert(pf_read_le64(clock_bytes + 1) >= first && GetLastError() == 0x4321);
    pf_wine_close(&api);
    if (printf("%016" PRIx64 "\n", digest) < 0 || fflush(stdout) != 0) {
        perror("PortForge could not write Wine evidence");
        return 1;
    }
    return 0;
}
