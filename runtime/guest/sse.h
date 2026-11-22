
/* Guest x86-64 SSE state. Include after the PFIR state and memory helpers. */
#define PF_MXCSR_RESET UINT32_C(0x1f80)
#define PF_MXCSR_MASK UINT32_C(0xffff)

static inline bool pf_mxcsr_memory(struct pf_state *state, uint64_t address, bool store) {
    uint8_t *bytes;
    if (!pf_guest_span(state, address, 4, 1, store, &bytes)) return false;
    if (store) {
        pf_write_le32(bytes, state->mxcsr);
        return true;
    }
    uint32_t value = pf_read_le32(bytes);
    if (value & ~PF_MXCSR_MASK) {
        state->simd_fault = PF_SIMD_BAD_MXCSR;
        return false;
    }
    state->mxcsr = value;
    return true;
}

static inline void pf_copy_xmm_scalar(struct pf_state *state, unsigned destination,
                                      unsigned source, unsigned width) {
    uint64_t *dst = &state->xmm[destination].qword[0];
    uint64_t src = state->xmm[source].qword[0];
    *dst = width == 64 ? src : (*dst & ~UINT64_C(0xffffffff)) | (uint32_t)src;
}

static inline bool pf_xmm_scalar_memory(struct pf_state *state, uint64_t address,
                                        unsigned reg, unsigned width, bool store) {
    uint8_t *bytes;
    if (!pf_guest_span(state, address, width / 8, 1, store, &bytes)) return false;
    if (store) {
        uint64_t value = state->xmm[reg].qword[0];
        if (width == 64) pf_write_le64(bytes, value);
        else pf_write_le32(bytes, (uint32_t)value);
    } else {
        uint64_t value = width == 64 ? pf_read_le64(bytes) : pf_read_le32(bytes);
        state->xmm[reg] = (struct pf_xmm){{value, 0}};
    }
    return true;
}
