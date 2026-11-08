#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__)
#include <sys/mman.h>
#endif
#include "lifted.c"

#define STACK_BASE UINT64_C(0x70000000)
#define STACK_SIZE 128
#define STOP_RIP UINT64_C(0x70001000)

static struct pf_state caller(uint8_t *stack, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    memset(stack, 0, STACK_SIZE);
    pf_write_le64(stack + STACK_SIZE - 8, STOP_RIP);
    return (struct pf_state){
        .gpr = {[1] = a, [2] = b, [4] = STACK_BASE + STACK_SIZE - 8, [8] = c, [9] = d},
        .rip = PF_PE_ENTRY,
        .memory = {stack, STACK_BASE, STACK_SIZE, true},
    };
}

static uint64_t next(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return *seed;
}

static void check_stops(void) {
    uint8_t stack[STACK_SIZE], original[STACK_SIZE];
    struct pf_state state = caller(stack, 3, 3, 0, 0);
    assert(pf_pe_run(&state, STOP_RIP, 0) == PF_PE_BUDGET && state.rip == PF_PE_ENTRY);
    assert(pf_pe_run(&state, STOP_RIP, 2) == PF_PE_BUDGET);
    uint64_t call_rip = state.rip, rsp = state.gpr[4];
    memcpy(original, stack, sizeof(stack));
    state.memory.writable = false;
    assert(pf_pe_run(&state, STOP_RIP, 16) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_READ_ONLY && state.fault_address == rsp - 8);
    assert(state.rip == call_rip && state.gpr[4] == rsp);
    assert(memcmp(stack, original, sizeof(stack)) == 0);

    state = caller(stack, 1, 2, 0, 0);
    assert(pf_pe_run(&state, STOP_RIP, 5) == PF_PE_BUDGET);
    uint64_t ret_rip = state.rip;
    state.memory.size -= 1;
    assert(pf_pe_run(&state, STOP_RIP, 1) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_UNMAPPED && state.fault_address == rsp);
    assert(state.rip == ret_rip && state.gpr[4] == rsp);

    state = caller(stack, 1, 2, 0, 0);
    uint64_t noncanonical = UINT64_C(0x0000800000000000);
    pf_write_le64(stack + STACK_SIZE - 8, noncanonical);
    assert(pf_pe_run(&state, STOP_RIP, 6) == PF_PE_MEMORY_FAULT);
    assert(state.memory_fault == PF_MEM_NONCANONICAL && state.fault_address == noncanonical);
    assert(state.rip == ret_rip && state.gpr[4] == rsp);

    state = caller(stack, 1, 2, 0, 0);
    pf_write_le64(stack + STACK_SIZE - 8, STOP_RIP + 8);
    assert(pf_pe_run(&state, STOP_RIP, 16) == PF_PE_UNKNOWN_RIP);
    assert(state.rip == STOP_RIP + 8 && state.gpr[4] == STACK_BASE + STACK_SIZE);
}

#if defined(__x86_64__)
typedef uint64_t (__attribute__((ms_abi)) *fixture_fn)(uint64_t, uint64_t, uint64_t, uint64_t);

static void *load_fixture(const char *path) {
    uint8_t code[4096];
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(code, 1, sizeof(code), file);
    assert(size && feof(file) && !ferror(file));
    assert(fclose(file) == 0);
    void *entry = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(entry != MAP_FAILED);
    memcpy(entry, code, size);
    assert(mprotect(entry, size, PROT_READ | PROT_EXEC) == 0);
    return entry;
}
#endif

int main(int argc, char **argv) {
#if defined(__x86_64__)
    assert(argc == 2);
    fixture_fn native = (fixture_fn)load_fixture(argv[1]);
#else
    (void)argv;
    assert(argc == 1);
#endif
    assert(pf_pe_bind_imports(NULL, NULL, 0, 0, NULL, 0).status == PF_PE_BIND_OK);
    check_stops();
    uint64_t seed = UINT64_C(0xd1486232cc719ac5);
    uint64_t digest = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 1000000; ++i) {
        uint64_t a = next(&seed), b = next(&seed), c = next(&seed), d = next(&seed);
        if (i % 3 != 0) b = a;
        if (i % 3 == 2) d = 0;
        uint8_t stack[STACK_SIZE];
        struct pf_state state = caller(stack, a, b, c, d);
        uint64_t expected = a != b ? a - c : d ? a + d - c : a;
        assert(pf_pe_run(&state, STOP_RIP, 16) == PF_PE_RETURNED);
        assert(state.rip == STOP_RIP && state.gpr[4] == STACK_BASE + STACK_SIZE);
        assert(state.gpr[0] == expected);
#if defined(__x86_64__)
        assert(state.gpr[0] == native(a, b, c, d));
#endif
        uint64_t pushed = pf_read_le64(stack + STACK_SIZE - 16);
        assert((a == b) == (pushed != 0));
        digest = (digest ^ state.gpr[0]) * UINT64_C(1099511628211);
        digest = (digest ^ pushed) * UINT64_C(1099511628211);
    }
    printf("%016" PRIx64 "\n", digest);
}
