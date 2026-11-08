#include "memory.h"
#include <assert.h>
#include <stdio.h>

static void reject(const struct pf_memory *memory, uint64_t address, size_t width,
                   size_t alignment, bool write, enum pf_memory_fault expected) {
    uint8_t sentinel;
    uint8_t *output = &sentinel;
    assert(pf_memory_span(memory, address, width, alignment, write, &output) == expected);
    assert(output == &sentinel);
}

int main(void) {
    uint8_t bytes[32] = {0};
    struct pf_memory memory = {bytes, 0x1000, sizeof(bytes), false};
    uint8_t *output = NULL;
    assert(pf_memory_span(&memory, 0x1010, 16, 16, false, &output) == PF_MEM_OK);
    assert(output == bytes + 16);
    reject(&memory, 0xfff, 1, 1, false, PF_MEM_UNMAPPED);
    reject(&memory, 0x101f, 2, 1, false, PF_MEM_UNMAPPED);
    reject(&memory, 0x1020, 1, 1, false, PF_MEM_UNMAPPED);
    reject(&memory, 0x1011, 8, 16, false, PF_MEM_ALIGNMENT);
    reject(&memory, 0x1000, 8, 8, true, PF_MEM_READ_ONLY);
    reject(&memory, UINT64_C(0x0000800000000000), 1, 1, false, PF_MEM_NONCANONICAL);
    reject(&memory, UINT64_C(0x00007fffffffffff), 2, 1, false, PF_MEM_NONCANONICAL);
    reject(&memory, UINT64_MAX, 2, 1, false, PF_MEM_NONCANONICAL);
    reject(&memory, UINT64_C(0xffff800000000000), 1, 1, false, PF_MEM_UNMAPPED);
    reject(&memory, 0x1000, 0, 1, false, PF_MEM_NONCANONICAL);
    memory.bytes = NULL;
    reject(&memory, 0x1000, 1, 1, false, PF_MEM_UNMAPPED);
    memory = (struct pf_memory){bytes, UINT64_MAX - 31, 32, true};
    assert(pf_memory_span(&memory, UINT64_MAX - 7, 8, 8, true, &output) == PF_MEM_OK);
    pf_write_le64(output, UINT64_C(0x0123456789abcdef));
    assert(bytes[24] == 0xef && bytes[31] == 0x01);
    assert(pf_read_le32(output) == UINT32_C(0x89abcdef));
    assert(pf_read_le64(output) == UINT64_C(0x0123456789abcdef));
    puts("guest memory bounds, permissions, alignment and byte order passed");
}
