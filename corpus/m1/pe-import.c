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
    {UINT64_C(0x70000000), by_name},
    {UINT64_C(0x70000008), by_ordinal},
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

static win_fn load_fixture(const char *path) {
    uint8_t *image = mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(image != MAP_FAILED);
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(image, 1, REGION_SIZE, file);
    assert(size > slot_offset(1) + 8 && feof(file) && !ferror(file));
    assert(fclose(file) == 0);
    pf_write_le64(image + slot_offset(0), (uintptr_t)(win_fn)native_name);
    pf_write_le64(image + slot_offset(1), (uintptr_t)(win_fn)native_ordinal);
    assert(mprotect(image, REGION_SIZE, PROT_READ | PROT_EXEC) == 0);
    return (win_fn)(image + (PF_PE_ENTRY - IMAGE_BASE));
}
#endif

int main(int argc, char **argv) {
    assert(pf_pe_import_count == 2);
    assert(strcmp(pf_pe_imports[0].dll, "fixture.dll") == 0);
    assert(strcmp(pf_pe_imports[0].name, "MixFour") == 0 && pf_pe_imports[0].ordinal == 0);
    assert(strcmp(pf_pe_imports[1].dll, "fixture.dll") == 0);
    assert(!pf_pe_imports[1].name && pf_pe_imports[1].ordinal == 17);
#if defined(__x86_64__)
    assert(argc == 2);
    win_fn native = load_fixture(argv[1]);
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
}
