#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "lifted.c"

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    uint8_t memory[PF_PE_IMAGE_SIZE + 64] = {0};
    assert(fread(memory, 1, PF_PE_IMAGE_SIZE, file) == PF_PE_IMAGE_SIZE);
    assert(fgetc(file) == EOF && !ferror(file) && fclose(file) == 0);
    const uint64_t stop = 0x10000;
    pf_write_le64(memory + sizeof(memory) - 8, stop);
    struct pf_state state = {
        .gpr = {[4] = PF_PE_IMAGE_BASE + sizeof(memory) - 8},
        .rip = PF_PE_ENTRY, .memory = {memory, PF_PE_IMAGE_BASE, sizeof(memory), true},
    };
    assert(pf_pe_run(&state, stop, 32) == PF_PE_RETURNED);
    assert(state.gpr[0] == UINT64_C(0x0123456789abcdef));
    printf("%016" PRIx64 " %016" PRIx64 "\n", PF_PE_IMAGE_BASE, state.gpr[0]);
}
