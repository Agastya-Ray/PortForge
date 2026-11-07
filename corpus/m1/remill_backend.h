#include "../../lift/spike/src/guest.h"

extern void pf_remill_execute(unsigned index, uint64_t registers[16],
                              uint64_t *rip, uint64_t *flags);

#define PF_TRACE(index, symbol) \
    static void pf_block_##index(struct pf_state *state) { \
        pf_remill_execute(index, state->gpr, &state->rip, &state->rflags); \
    }
#include "remill_entries.inc"
#undef PF_TRACE
