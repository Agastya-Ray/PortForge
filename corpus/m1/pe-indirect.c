#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#if defined(__x86_64__)
#include <sys/mman.h>
#endif
#include "lifted.c"
#include "memory-evidence.h"

#define STACK_BASE UINT64_C(0x60000000)
#define STOP UINT64_C(0x70000000)
#define TARGET_ADD (PF_PE_ENTRY + UINT64_C(0x80))
#define TARGET_XOR (PF_PE_ENTRY + UINT64_C(0x90))
#define SIZE 128

static struct pf_state caller(uint8_t *stack, uint64_t target, bool reg, uint64_t a, uint64_t b) {
    memset(stack, 0, SIZE);
    pf_write_le64(stack + SIZE - 8, STOP);
    return (struct pf_state){
        .gpr = {[1] = target, [2] = reg, [4] = STACK_BASE + SIZE - 8, [8] = a, [9] = b},
        .rip = PF_PE_ENTRY, .memory = {stack, STACK_BASE, SIZE, true},
    };
}

static void rejected_call(uint64_t target, bool reg, enum pf_pe_status expected, enum pf_memory_fault fault) {
    uint8_t stack[SIZE], previous[SIZE];
    struct pf_state state = caller(stack, target, reg, 31, 65);
    assert(pf_pe_run(&state, STOP, reg ? 3 : 4) == PF_PE_BUDGET);
    memcpy(previous, stack, SIZE);
    uint64_t rip = state.rip, rsp = state.gpr[4];
    assert(pf_pe_run(&state, STOP, 1) == expected);
    assert(state.memory_fault == fault && state.rip == rip && state.gpr[4] == rsp);
    assert(memcmp(stack, previous, SIZE) == 0);
}

static void check_stops(void) {
    for (unsigned reg = 0; reg < 2; ++reg) {
        rejected_call(TARGET_ADD + 1, reg, PF_PE_UNKNOWN_RIP, PF_MEM_OK);
        rejected_call(TARGET_XOR + 8, reg, PF_PE_UNKNOWN_RIP, PF_MEM_OK);
        rejected_call(UINT64_C(0x800000000000), reg, PF_PE_MEMORY_FAULT, PF_MEM_NONCANONICAL);
        rejected_call(STOP, reg, PF_PE_UNBOUND_IMPORT, PF_MEM_OK);
    }
    uint8_t stack[SIZE], previous[SIZE];
    struct pf_state state = caller(stack, TARGET_XOR, false, 0, 0);
    assert(pf_pe_run(&state, STOP, 4) == PF_PE_BUDGET);
    uint64_t rip = state.rip, rsp = state.gpr[4];
    memcpy(previous, stack, SIZE);
    state.memory.size = rsp - STACK_BASE + 31;
    assert(pf_pe_run(&state, STOP, 1) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == rsp + 24);
    assert(state.rip == rip && state.gpr[4] == rsp && memcmp(stack, previous, SIZE) == 0);
    state.memory.size = SIZE;
    state.memory.writable = false;
    state.memory_fault = PF_MEM_OK;
    assert(pf_pe_run(&state, STOP, 1) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.fault_address == rsp - 8);
    assert(state.rip == rip && state.gpr[4] == rsp && memcmp(stack, previous, SIZE) == 0);
}

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

int main(int argc, char **argv) {
#if defined(__x86_64__)
    typedef uint64_t (__attribute__((ms_abi)) *entry_fn)(void *, uint64_t, uint64_t, uint64_t);
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    uint8_t *native = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(native != MAP_FAILED);
    size_t size = fread(native, 1, 4096, file);
    assert(size > 0x90 && feof(file) && !ferror(file));
    assert(fclose(file) == 0 && mprotect(native, 4096, PROT_READ | PROT_EXEC) == 0);
    entry_fn entry = (entry_fn)(void *)native;
#else
    (void)argv;
    assert(argc == 1);
#endif
    check_stops();
    uint64_t digest = PF_MEMORY_DIGEST_SEED, seed = UINT64_C(0x3a2059e2ccb5c18d);
    for (unsigned i = 0; i < 1000000; ++i) {
        bool reg = i & 1, add = i & 2;
        uint64_t a = next(&seed), b = next(&seed);
        uint8_t stack[SIZE];
        struct pf_state state = caller(stack, add ? TARGET_ADD : TARGET_XOR, reg, a, b);
        assert(pf_pe_run(&state, STOP, 32) == PF_PE_RETURNED);
        assert(state.rip == STOP && state.gpr[4] == STACK_BASE + SIZE);
        assert(state.gpr[0] == (add ? a + b : a ^ b));
#if defined(__x86_64__)
        assert(state.gpr[0] == entry(native + (add ? 0x80 : 0x90), reg, a, b));
#endif
        digest = pf_memory_digest(digest, state.gpr[0]);
    }
#if defined(__x86_64__)
    assert(munmap(native, 4096) == 0);
#endif
    pf_memory_print_digest(digest);
}
