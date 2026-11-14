#include <assert.h>
#include "lifted.c"
int main(void) {
    uint8_t memory[64] = {0};
    const uint64_t stop = UINT64_C(0x9000);
    pf_write_le64(memory, stop);
    struct pf_state s = {.gpr = {[4] = 0x8000}, .rip = PF_PE_ENTRY, .rflags = 0x202,
        .memory = {memory, 0x8000, sizeof(memory), true}};
    enum pf_pe_status status = pf_pe_run(&s, stop, 5);
#if EXPECT_FAULT
    assert(status == PF_PE_INTEGER_FAULT && s.integer_fault == EXPECT_FAULT);
    assert(s.rip == PF_PE_ENTRY + 30 && s.rflags == 0x202 && s.pending.operation == PF_NO_FLAGS);
    assert(s.gpr[4] == 0x8000 && pf_pe_run(&s, stop, 0) == PF_PE_INTEGER_FAULT);
    s.memory_fault = PF_MEM_UNMAPPED;
    assert(pf_pe_run(&s, stop, 0) == PF_PE_MEMORY_FAULT);
#else
    assert(status == PF_PE_RETURNED && s.integer_fault == PF_INT_OK);
    assert(s.gpr[0] == 10 && s.gpr[2] == 7 && s.gpr[4] == 0x8008 && s.rflags == 0x202);
#endif
}
