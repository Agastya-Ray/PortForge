#include <stdio.h>
#include "lifted.c"
#include "wine_imports.h"

enum { PF_STACK_SIZE = 64 * 1024, PF_INSTRUCTION_BUDGET = 1000000 };

static bool read_image(const char *path, uint8_t *memory) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool exact = fread(memory, 1, PF_PE_IMAGE_SIZE, file) == PF_PE_IMAGE_SIZE
        && fgetc(file) == EOF && !ferror(file);
    return fclose(file) == 0 && exact;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: wine_console <mapped PE image>\n", stderr);
        return 125;
    }
    size_t size = (size_t)PF_PE_IMAGE_SIZE + PF_STACK_SIZE;
    uint64_t base = PF_PE_IMAGE_BASE;
    if (size > UINT64_MAX - base || !pf_canonical48(base + size - 1)
        || ((base ^ (base + size - 1)) >> 63)) {
        fputs("PortForge could not place the guest stack after the PE image\n", stderr);
        return 125;
    }
    uint8_t *memory = calloc(size, 1);
    struct pf_pe_binding *bindings = calloc(pf_pe_import_count ? pf_pe_import_count : 1, sizeof(*bindings));
    struct pf_wine_api api = {0};
    int result = 125;
    if (!memory || !bindings || !read_image(argv[1], memory)) {
        fprintf(stderr, "PortForge could not read exactly %u mapped image bytes\n", PF_PE_IMAGE_SIZE);
        goto done;
    }
    DWORD error = pf_wine_open(&api);
    if (error != ERROR_SUCCESS) {
        fprintf(stderr, "PortForge Wine export lookup failed: %lu\n", (unsigned long)error);
        goto done;
    }
    /* Keep import tokens and the return sentinel outside the mapped image and stack. */
    uint64_t stop = base >> 63 ? UINT64_C(0x10000) : UINT64_C(0xffff800000010000);
    uint64_t rsp = ((base + size) & ~UINT64_C(15)) - 40;
    pf_write_le64(memory + (rsp - base), stop);
    struct pf_state state = {
        .gpr = {[4] = rsp}, .rip = PF_PE_ENTRY, .rflags = 2,
        .memory = {memory, base, size, true},
    };
    struct pf_pe_bind_result bind = pf_pe_bind_imports(&state, api.exports, PF_WINE_EXPORT_COUNT,
        stop + 8, bindings, pf_pe_import_count);
    if (bind.status != PF_PE_BIND_OK) {
        fprintf(stderr, "PortForge could not bind imports (status %d, import %zu)\n", bind.status, bind.import_index);
        goto done;
    }
    enum pf_pe_status status = pf_pe_run_bound(&state, stop, PF_INSTRUCTION_BUDGET, bindings, pf_pe_import_count);
    if (status == PF_PE_EXITED) result = (int)state.pe_exit_code;
    else if (status == PF_PE_RETURNED) result = (int)(uint32_t)state.gpr[0];
    else fprintf(stderr, "PortForge guest stopped at 0x%llx (status %d, memory fault %d at 0x%llx)\n",
        (unsigned long long)state.rip, status, state.memory_fault, (unsigned long long)state.fault_address);
done:
    pf_wine_close(&api);
    free(bindings);
    free(memory);
    return result;
}
