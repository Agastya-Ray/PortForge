#include <assert.h>
#include "lifted.c"

#define SIZE (PF_PE_IMAGE_SIZE + 128u)
#define BASE PF_PE_IMAGE_BASE
#define FIRST_IMPORT UINT64_C(0x70000000)
#define NEXT (PF_PE_ENTRY + 8)
#define INPUT 128
#define WRITTEN 160

static uint8_t memory[SIZE];
static unsigned host_calls;

static uint64_t host_write(void *context, uint64_t handle, uint64_t bytes, uint64_t count, uint64_t written) {
    assert(context == memory && handle == 1 && (uint32_t)count == 4);
    assert(bytes == (uintptr_t)(memory + INPUT) && written == (uintptr_t)(memory + WRITTEN));
    assert(pf_read_le32(memory + INPUT) == UINT32_C(0xabcdef01));
    pf_write_le32(memory + WRITTEN, (uint32_t)count);
    ++host_calls;
    return 1;
}

static uint64_t host_exit(void *context, uint64_t code, uint64_t b, uint64_t c, uint64_t d) {
    (void)context;
    (void)b;
    (void)c;
    (void)d;
    ++host_calls;
    return (uint32_t)code;
}

static const struct pf_pe_export exports[] = {
    {.dll = "kernel32.dll", .name = "GetStdHandle", .invoke = host_exit},
    {.dll = "kernel32.dll", .name = "ExitProcess", .invoke = host_exit, .exits = true},
    {
        .dll = "kernel32.dll", .name = "WriteFile", .invoke = host_write, .context = memory,
        .args = {{0}, {.size_arg = 3}, {0}, {.size = 4, .writable = true}}, .null_fifth = true,
    },
};

static size_t find_import(const char *name) {
    for (size_t i = 0; i < pf_pe_import_count; ++i) {
        if (pf_pe_imports[i].name && strcmp(pf_pe_imports[i].name, name) == 0) return i;
    }
    abort();
}

static struct pf_state caller(void) {
    return (struct pf_state){
        .gpr = {[0] = UINT64_MAX, [1] = 1, [2] = BASE + INPUT, [4] = BASE + SIZE - 48,
                [8] = UINT64_C(0xffffffff00000004), [9] = BASE + WRITTEN},
        .rip = PF_PE_ENTRY,
        .memory = {memory, BASE, sizeof(memory), true},
    };
}

static void check_rejected(struct pf_state state, size_t index, const struct pf_pe_binding *bindings,
                           enum pf_pe_status status, enum pf_memory_fault fault, uint64_t address) {
    uint8_t saved[sizeof(memory)];
    memcpy(saved, memory, sizeof(memory));
    uint64_t rsp = state.gpr[4];
    assert(pf_pe_import_call(&state, pf_pe_imports[index].slot, bindings + index, NEXT) == status);
    assert(state.memory_fault == fault && state.fault_address == address);
    assert(state.gpr[0] == UINT64_MAX && state.gpr[4] == rsp && state.rip == PF_PE_ENTRY);
    assert(!host_calls && memcmp(saved, memory, sizeof(memory)) == 0);
}

int main(void) {
    struct pf_state state = caller();
    struct pf_pe_binding bindings[3];
    assert(pf_pe_import_count == 3);
    size_t write = find_import("WriteFile"), exit = find_import("ExitProcess");
    struct pf_pe_export returning[3];
    memcpy(returning, exports, sizeof(returning));
    returning[1].exits = false;
    struct pf_pe_bind_result bind = pf_pe_bind_imports(&state, returning, 3, FIRST_IMPORT, bindings, 3);
    assert(bind.status == PF_PE_BIND_INVALID && bind.import_index == exit);
    for (size_t i = 0; i < pf_pe_import_count; ++i) {
        assert(pf_read_le64(memory + pf_pe_imports[i].slot - BASE) == 0);
    }
    assert(pf_pe_bind_imports(&state, exports, 3, FIRST_IMPORT, bindings, 3).status == PF_PE_BIND_OK);

    size_t fifth = state.gpr[4] + 32 - BASE;
    pf_write_le64(memory + fifth, BASE + INPUT);
    check_rejected(state, write, bindings, PF_PE_UNSUPPORTED_IMPORT, PF_MEM_OK, 0);
    pf_write_le64(memory + fifth, 0);
    state.gpr[4] = BASE + SIZE - 32;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_UNMAPPED, BASE + SIZE);
    state = caller();
    state.gpr[2] = BASE + SIZE - 3;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_UNMAPPED, state.gpr[2]);
    state = caller();
    state.gpr[9] = 0;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_UNMAPPED, 0);
    state = caller();
    state.memory.writable = false;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_READ_ONLY, BASE + WRITTEN);
    bindings[write].args[1].writable = true;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_READ_ONLY, BASE + INPUT);
    bindings[write].args[1].writable = false;
    state = caller();
    state.gpr[4] = BASE;
    check_rejected(state, write, bindings, PF_PE_MEMORY_FAULT, PF_MEM_UNMAPPED, BASE - 8);

    state = caller();
    assert(pf_pe_register_call(&state, FIRST_IMPORT + 24, bindings, 3, NEXT) == PF_PE_UNBOUND_IMPORT);
    assert(pf_pe_register_call(&state, bindings[write].target, bindings, write, NEXT) == PF_PE_UNBOUND_IMPORT);
    assert(!host_calls && state.rip == PF_PE_ENTRY);
    pf_write_le32(memory + INPUT, UINT32_C(0xabcdef01));
    pf_write_le64(memory + pf_pe_imports[write].slot - BASE, 0);
    assert(pf_pe_register_call(&state, bindings[write].target, bindings, 3, NEXT) == PF_PE_CONTINUE);
    assert(host_calls == 1 && state.rip == NEXT && state.gpr[0] == 1);
    assert(state.gpr[4] == BASE + SIZE - 48 && pf_read_le32(memory + WRITTEN) == 4);

    state = caller();
    state.gpr[1] = UINT64_C(0xffffffff87654321);
    assert(pf_pe_import_call(&state, pf_pe_imports[exit].slot, bindings + exit, NEXT) == PF_PE_EXITED);
    assert(host_calls == 2 && state.pe_exit_code == UINT32_C(0x87654321));
    assert(pf_pe_run_bound(&state, NEXT, 0, bindings, 3) == PF_PE_EXITED);
    assert(host_calls == 2);
}
