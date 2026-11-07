#include "../../lift/spike/src/guest.h"

extern void pf_remill_execute(unsigned index, uint64_t registers[16],
                              uint64_t *rip, uint64_t *flags, void *xmm);

#ifdef PF_REMILL_XMM
#define PF_XMM_STATE(state) ((state)->xmm)
#else
#define PF_XMM_STATE(state) ((void *)0)
#endif

#define PF_TRACE(index, symbol) \
    static void pf_block_##index(struct pf_state *state) { \
        pf_remill_execute(index, state->gpr, &state->rip, &state->rflags, PF_XMM_STATE(state)); \
    }
#include "remill_entries.inc"
#undef PF_TRACE
#undef PF_XMM_STATE
