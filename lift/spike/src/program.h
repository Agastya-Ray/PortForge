
enum pf_pe_status {
    PF_PE_RETURNED,
    PF_PE_UNKNOWN_RIP,
    PF_PE_BUDGET,
    PF_PE_MEMORY_FAULT,
    PF_PE_X87_UNSUPPORTED,
};

static inline void pf_pe_call(struct pf_state *state, uint64_t target, uint64_t return_rip) {
    uint64_t rsp = state->gpr[4] - 8;
    uint8_t *bytes;
    if (!pf_guest_span(state, rsp, 8, 1, true, &bytes)) return;
    pf_write_le64(bytes, return_rip);
    state->gpr[4] = rsp;
    state->rip = target;
}

static inline void pf_pe_return(struct pf_state *state) {
    uint8_t *bytes;
    if (!pf_guest_span(state, state->gpr[4], 8, 1, false, &bytes)) return;
    uint64_t target = pf_read_le64(bytes);
    if (!pf_canonical48(target)) {
        state->memory_fault = PF_MEM_NONCANONICAL;
        state->fault_address = target;
        return;
    }
    state->gpr[4] += 8;
    state->rip = target;
}
