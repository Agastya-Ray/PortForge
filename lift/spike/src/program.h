
enum pf_pe_status {
    PF_PE_RETURNED,
    PF_PE_UNKNOWN_RIP,
    PF_PE_BUDGET,
    PF_PE_MEMORY_FAULT,
    PF_PE_X87_UNSUPPORTED,
    PF_PE_UNBOUND_IMPORT,
};

struct pf_pe_import {
    uint64_t slot;
    const char *dll;
    const char *name;
    uint16_t ordinal;
};

typedef uint64_t (*pf_pe_host_fn)(uint64_t, uint64_t, uint64_t, uint64_t);

struct pf_pe_binding {
    uint64_t target;
    pf_pe_host_fn invoke;
};

static inline void pf_pe_call(struct pf_state *state, uint64_t target, uint64_t return_rip) {
    uint64_t rsp = state->gpr[4] - 8;
    uint8_t *bytes;
    if (!pf_guest_span(state, rsp, 8, 1, true, &bytes)) return;
    pf_write_le64(bytes, return_rip);
    state->gpr[4] = rsp;
    state->rip = target;
}

static inline bool pf_pe_check_target(struct pf_state *state, uint64_t target) {
    if (pf_canonical48(target)) return true;
    state->memory_fault = PF_MEM_NONCANONICAL;
    state->fault_address = target;
    return false;
}

static inline void pf_pe_return(struct pf_state *state) {
    uint8_t *bytes;
    if (!pf_guest_span(state, state->gpr[4], 8, 1, false, &bytes)) return;
    uint64_t target = pf_read_le64(bytes);
    if (!pf_pe_check_target(state, target)) return;
    state->gpr[4] += 8;
    state->rip = target;
}

static inline bool pf_pe_import_call(struct pf_state *state, uint64_t slot,
                                     const struct pf_pe_binding *binding, uint64_t return_rip) {
    uint8_t *bytes;
    if (!pf_guest_span(state, slot, 8, 1, false, &bytes)) return false;
    uint64_t target = pf_read_le64(bytes);
    if (!pf_pe_check_target(state, target)) return false;
    if (!target || !binding || !binding->invoke || binding->target != target) return false;
    pf_pe_call(state, target, return_rip);
    if (state->memory_fault != PF_MEM_OK) return false;
    state->gpr[0] = binding->invoke(state->gpr[1], state->gpr[2], state->gpr[8], state->gpr[9]);
    state->gpr[4] += 8;
    state->rip = return_rip;
    return true;
}
