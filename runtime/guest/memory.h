#ifndef PF_MEMORY_H
#define PF_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pf_memory {
    uint8_t *bytes;
    uint64_t base;
    size_t size;
    bool writable;
};

enum pf_memory_fault {
    PF_MEM_OK, PF_MEM_NONCANONICAL, PF_MEM_ALIGNMENT, PF_MEM_UNMAPPED, PF_MEM_READ_ONLY,
};

static inline bool pf_canonical48(uint64_t address) {
    return address <= UINT64_C(0x00007fffffffffff)
        || address >= UINT64_C(0xffff800000000000);
}

/* Alignment is a nonzero power of two. Leave out untouched on failure. */
static inline enum pf_memory_fault pf_memory_span(
    const struct pf_memory *memory, uint64_t address, size_t width,
    size_t alignment, bool write, uint8_t **out
) {
    if (!width || width - 1 > UINT64_MAX - address) return PF_MEM_NONCANONICAL;
    uint64_t end = address + width - 1;
    if (!pf_canonical48(address) || !pf_canonical48(end) || ((address ^ end) >> 63)) {
        return PF_MEM_NONCANONICAL;
    }
    if (!alignment || (alignment & (alignment - 1)) || address % alignment) {
        return PF_MEM_ALIGNMENT;
    }
    if (!memory->bytes || address < memory->base) return PF_MEM_UNMAPPED;
    uint64_t offset = address - memory->base;
    if (offset > memory->size || width > memory->size - offset) return PF_MEM_UNMAPPED;
    if (write && !memory->writable) return PF_MEM_READ_ONLY;
    *out = memory->bytes + (size_t)offset;
    return PF_MEM_OK;
}

static inline uint32_t pf_read_le32(const uint8_t *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)bytes[i] << (8 * i);
    return value;
}

static inline uint64_t pf_read_le64(const uint8_t *bytes) {
    return pf_read_le32(bytes) | ((uint64_t)pf_read_le32(bytes + 4) << 32);
}

static inline void pf_write_le32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
}

static inline void pf_write_le64(uint8_t *bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
}

#endif
