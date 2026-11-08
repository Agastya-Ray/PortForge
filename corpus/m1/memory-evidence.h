#ifndef PF_MEMORY_EVIDENCE_H
#define PF_MEMORY_EVIDENCE_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define PF_MEMORY_DIGEST_SEED UINT64_C(14695981039346656037)

static inline uint64_t pf_memory_digest(uint64_t digest, uint64_t value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

static inline void pf_memory_print_digest(uint64_t digest) {
    printf("%016" PRIx64 "\n", digest);
}

#endif
