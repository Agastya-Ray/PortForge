#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__)
#include <sys/mman.h>
#endif
#include "lifted.c"

#define IMAGE_BASE UINT64_C(0x140000000)
#define REGION_SIZE 0x10000
#define STACK_END (IMAGE_BASE + REGION_SIZE)
#define STOP_RIP UINT64_C(0x70001000)

static unsigned host_calls;

static uint64_t by_name(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    ++host_calls;
    return (a ^ (b + c * 3)) - d;
}

static uint64_t by_ordinal(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    ++host_calls;
    return ((a - c) ^ d) + b * 5;
}

static const struct pf_pe_binding bindings[] = {
    {.target = UINT64_C(0x70000000), .invoke = by_name},
    {.target = UINT64_C(0x70000008), .invoke = by_ordinal},
};

static uint64_t pointers_by_name(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    ++host_calls;
    uint64_t result = pf_read_le64((const uint8_t *)(uintptr_t)b) ^ c;
    pf_write_le64((uint8_t *)(uintptr_t)a, result);
    pf_write_le64((uint8_t *)(uintptr_t)d, result + pf_read_le64((const uint8_t *)(uintptr_t)b));
    return result;
}

static uint64_t pointers_by_ordinal(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    ++host_calls;
    uint64_t result = pf_read_le64((const uint8_t *)(uintptr_t)b) + c;
    pf_write_le64((uint8_t *)(uintptr_t)a, result);
    result ^= pf_read_le64((const uint8_t *)(uintptr_t)b);
    pf_write_le64((uint8_t *)(uintptr_t)d, result);
    return result;
}

static const struct pf_pe_binding pointer_bindings[] = {
    {
        .target = UINT64_C(0x70000000), .invoke = pointers_by_name,
        .args = {{8, true}, {8, false}, {0}, {8, true}},
    },
    {
        .target = UINT64_C(0x70000008), .invoke = pointers_by_ordinal,
        .args = {{8, true}, {8, false}, {0}, {8, true}},
    },
};

static size_t slot_offset(unsigned index) {
    assert(index < pf_pe_import_count);
    assert(pf_pe_imports[index].slot >= IMAGE_BASE && pf_pe_imports[index].slot < STACK_END - 8);
    return pf_pe_imports[index].slot - IMAGE_BASE;
}

static struct pf_state caller(uint8_t *memory, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    pf_write_le64(memory + REGION_SIZE - 8, STOP_RIP);
    pf_write_le64(memory + REGION_SIZE - 56, 0);
    return (struct pf_state){
        .gpr = {[0] = 1234, [1] = a, [2] = b, [4] = STACK_END - 8, [8] = c, [9] = d},
        .rip = PF_PE_ENTRY,
        .memory = {memory, IMAGE_BASE, REGION_SIZE, true},
    };
}

static void check_stops(uint8_t *memory) {
    struct pf_state state = caller(memory, 1, 2, 3, 4);
    assert(pf_pe_run_bound(&state, STOP_RIP, 3, bindings, 2) == PF_PE_BUDGET);
    struct pf_state at_call = state;
    assert(pf_pe_run(&state, STOP_RIP, 1) == PF_PE_UNBOUND_IMPORT);
    assert(state.rip == at_call.rip && state.gpr[0] == 1234 && state.gpr[4] == at_call.gpr[4]);

    pf_write_le64(memory + slot_offset(0), bindings[1].target);
    assert(pf_pe_run_bound(&state, STOP_RIP, 1, bindings, 2) == PF_PE_UNBOUND_IMPORT);
    assert(state.rip == at_call.rip && state.gpr[0] == 1234 && state.gpr[4] == at_call.gpr[4]);
    pf_write_le64(memory + slot_offset(0), UINT64_C(0x0000800000000000));
    assert(pf_pe_run_bound(&state, STOP_RIP, 1, bindings, 2) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_NONCANONICAL && state.rip == at_call.rip);
    pf_write_le64(memory + slot_offset(0), bindings[0].target);

    state = at_call;
    state.memory.writable = false;
    assert(pf_pe_run_bound(&state, STOP_RIP, 1, bindings, 2) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.fault_address == at_call.gpr[4] - 8);
    assert(state.rip == at_call.rip && state.gpr[0] == 1234 && state.gpr[4] == at_call.gpr[4]);

    state = at_call;
    state.memory.size = slot_offset(0) + 7;
    assert(pf_pe_run_bound(&state, STOP_RIP, 1, bindings, 2) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == pf_pe_imports[0].slot);
    assert(state.rip == at_call.rip && state.gpr[0] == 1234 && state.gpr[4] == at_call.gpr[4]);
    assert(host_calls == 0 && pf_read_le64(memory + REGION_SIZE - 56) == 0);
}

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

#if defined(__x86_64__)
typedef uint64_t (__attribute__((ms_abi)) *win_fn)(uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t __attribute__((ms_abi)) native_name(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return by_name(a, b, c, d);
}

static uint64_t __attribute__((ms_abi)) native_ordinal(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return by_ordinal(a, b, c, d);
}

static uint64_t __attribute__((ms_abi)) native_pointer_name(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return pointers_by_name(a, b, c, d);
}

static uint64_t __attribute__((ms_abi)) native_pointer_ordinal(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return pointers_by_ordinal(a, b, c, d);
}

static win_fn load_fixture(const char *path, win_fn named, win_fn ordinal) {
    uint8_t *image = mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(image != MAP_FAILED);
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(image, 1, REGION_SIZE, file);
    assert(size > slot_offset(1) + 8 && feof(file) && !ferror(file));
    assert(fclose(file) == 0);
    pf_write_le64(image + slot_offset(0), (uintptr_t)named);
    pf_write_le64(image + slot_offset(1), (uintptr_t)ordinal);
    assert(mprotect(image, REGION_SIZE, PROT_READ | PROT_EXEC) == 0);
    return (win_fn)(image + (PF_PE_ENTRY - IMAGE_BASE));
}
#endif

static void check_pointer_stops(uint8_t *memory) {
    const uint64_t a = IMAGE_BASE + 0x301, b = IMAGE_BASE + 0x311, d = IMAGE_BASE + 0x321;
    const uint64_t faults[] = {0, STACK_END - 7, UINT64_C(0x0000800000000000), d};
    const enum pf_memory_fault codes[] = {
        PF_MEM_UNMAPPED, PF_MEM_UNMAPPED, PF_MEM_NONCANONICAL, PF_MEM_READ_ONLY,
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        struct pf_state state = caller(memory, faults[i], b, 7, d);
        if (codes[i] == PF_MEM_READ_ONLY) state.memory.writable = false;
        assert(pf_pe_run_bound(&state, STOP_RIP, 3, pointer_bindings, 2) == PF_PE_BUDGET);
        uint64_t call_rip = state.rip, rsp = state.gpr[4];
        pf_write_le64(memory + (a - IMAGE_BASE), 17);
        pf_write_le64(memory + (d - IMAGE_BASE), 19);
        unsigned before = host_calls;
        assert(pf_pe_run_bound(&state, STOP_RIP, 1, pointer_bindings, 2) == PF_PE_MEMORY_FAULT);
        assert(state.memory_fault == codes[i] && state.fault_address == faults[i]);
        assert(state.rip == call_rip && state.gpr[0] == 1234 && state.gpr[4] == rsp);
        assert(host_calls == before && pf_read_le64(memory + REGION_SIZE - 56) == 0);
        assert(pf_read_le64(memory + (a - IMAGE_BASE)) == 17);
        assert(pf_read_le64(memory + (d - IMAGE_BASE)) == 19);
    }

    // The fourth argument can fail after the other spans have been validated.
    struct pf_state state = caller(memory, a, b, 7, STACK_END - 7);
    unsigned before = host_calls;
    assert(pf_pe_run_bound(&state, STOP_RIP, 16, pointer_bindings, 2) == PF_PE_MEMORY_FAULT);
    assert(state.fault_address == STACK_END - 7 && state.gpr[0] == 1234 && host_calls == before);
    assert(pf_read_le64(memory + (a - IMAGE_BASE)) == 17);

    // A valid writable argument can alias the return address pushed by CALL.
    uint64_t invalid_target = UINT64_C(0x0000800000000000);
    pf_write_le64(memory + (b - IMAGE_BASE), invalid_target);
    state = caller(memory, STACK_END - 56, b, 0, d);
    assert(pf_pe_run_bound(&state, STOP_RIP, 16, pointer_bindings, 2) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_NONCANONICAL && state.fault_address == invalid_target);
    assert(state.rip == pointer_bindings[0].target && state.gpr[4] == STACK_END - 56);
    assert(state.gpr[0] == invalid_target && host_calls == before + 1);
    assert(pf_read_le64(memory + (d - IMAGE_BASE)) == invalid_target * 2);

    pf_write_le64(memory + (b - IMAGE_BASE), STOP_RIP);
    state = caller(memory, STACK_END - 56, b, 0, d);
    assert(pf_pe_run_bound(&state, STOP_RIP, 16, pointer_bindings, 2) == PF_PE_RETURNED);
    assert(state.rip == STOP_RIP && state.gpr[4] == STACK_END - 48);
    assert(state.gpr[0] == STOP_RIP && host_calls == before + 2);
}

static uint64_t check_pointers(uint8_t *memory, const char *path) {
#if defined(__x86_64__)
    win_fn native = load_fixture(path, native_pointer_name, native_pointer_ordinal);
#else
    (void)path;
#endif
    uint8_t *buffers = memory + 0x301;
    const uint64_t start = IMAGE_BASE + 0x301;
    uint64_t seed = UINT64_C(0x61afe192d1486232);
    uint64_t digest = UINT64_C(14695981039346656037);
    check_pointer_stops(memory);
    for (unsigned i = 0; i < 1000000; ++i) {
        unsigned b = i % 2 ? 0 : 8;
        unsigned d = i % 4 ? 16 : 4;
        uint64_t c = next(&seed);
        for (unsigned j = 0; j < 24; j += 8) pf_write_le64(buffers + j, next(&seed));
#if defined(__x86_64__)
        uint8_t native_buffers[24];
        memcpy(native_buffers, buffers, sizeof(native_buffers));
        uint64_t expected = native((uintptr_t)native_buffers, (uintptr_t)(native_buffers + b),
                                   c, (uintptr_t)(native_buffers + d));
#endif
        struct pf_state state = caller(memory, start, start + b, c, start + d);
        unsigned before = host_calls;
        assert(pf_pe_run_bound(&state, STOP_RIP, 16, pointer_bindings, 2) == PF_PE_RETURNED);
        assert(host_calls == before + 1 && state.gpr[4] == STACK_END && state.rip == STOP_RIP);
        assert(state.gpr[1] == start && state.gpr[2] == start + b && state.gpr[9] == start + d);
#if defined(__x86_64__)
        assert(state.gpr[0] == expected);
        assert(memcmp(buffers, native_buffers, sizeof(native_buffers)) == 0);
#endif
        digest = (digest ^ state.gpr[0]) * UINT64_C(1099511628211);
        for (unsigned j = 0; j < 24; j += 8) {
            digest = (digest ^ pf_read_le64(buffers + j)) * UINT64_C(1099511628211);
        }
    }
    return digest;
}

int main(int argc, char **argv) {
    assert(pf_pe_import_count == 2);
    assert(strcmp(pf_pe_imports[0].dll, "fixture.dll") == 0);
    assert(strcmp(pf_pe_imports[0].name, "MixFour") == 0 && pf_pe_imports[0].ordinal == 0);
    assert(strcmp(pf_pe_imports[1].dll, "fixture.dll") == 0);
    assert(!pf_pe_imports[1].name && pf_pe_imports[1].ordinal == 17);
#if defined(__x86_64__)
    assert(argc == 2);
    win_fn native = load_fixture(argv[1], native_name, native_ordinal);
#else
    (void)argv;
    assert(argc == 1);
#endif
    uint8_t memory[REGION_SIZE] = {0};
    for (unsigned i = 0; i < 2; ++i) pf_write_le64(memory + slot_offset(i), bindings[i].target);
    check_stops(memory);
    uint64_t seed = UINT64_C(0xd1486232cc719ac5);
    uint64_t digest = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed), c = next(&seed), d = next(&seed);
        if (i % 2 == 0) b = a;
        struct pf_state state = caller(memory, a, b, c, d);
        unsigned before = host_calls;
        assert(pf_pe_run_bound(&state, STOP_RIP, 16, bindings, 2) == PF_PE_RETURNED);
        assert(host_calls == before + 1 && state.gpr[4] == STACK_END);
        uint64_t expected = a == b ? ((a - c) ^ d) + b * 5 : (a ^ (b + c * 3)) - d;
        assert(state.gpr[0] == expected);
#if defined(__x86_64__)
        assert(state.gpr[0] == native(a, b, c, d));
#endif
        uint64_t return_rip = pf_read_le64(memory + REGION_SIZE - 56);
        assert(return_rip > PF_PE_ENTRY && return_rip < pf_pe_imports[0].slot);
        digest = (digest ^ state.gpr[0]) * UINT64_C(1099511628211);
        digest = (digest ^ return_rip) * UINT64_C(1099511628211);
    }
    printf("%016" PRIx64 "\n", digest);
    printf("%016" PRIx64 "\n", check_pointers(memory, argc == 2 ? argv[1] : NULL));
}
