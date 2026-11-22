
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

enum {
    PF_MXCSR_INVALID = 1,
    PF_MXCSR_DENORMAL = 2,
    PF_MXCSR_DAZ = 1 << 6,
    PF_MXCSR_MASK_SHIFT = 7,
};

static inline bool pf_sse_compare(struct pf_state *state, uint64_t lhs, uint64_t rhs,
                                  unsigned width, bool ordered) {
    uint64_t sign = UINT64_C(1) << (width - 1);
    uint64_t infinity = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    uint64_t quiet = width == 32 ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
    uint64_t magnitude_mask = sign - 1;
    uint64_t a = lhs & magnitude_mask, b = rhs & magnitude_mask;
    bool nan_a = a > infinity, nan_b = b > infinity;
    unsigned raised = 0;
    uint64_t flags;
    if (nan_a || nan_b) {
        // NaN classification takes precedence over denormal handling.
        if (ordered || (nan_a && !(a & quiet)) || (nan_b && !(b & quiet))) {
            raised = PF_MXCSR_INVALID;
        }
        flags = 0x45; // ZF | PF | CF
    } else {
        uint64_t least_normal = quiet << 1;
        bool denormal_a = a != 0 && a < least_normal;
        bool denormal_b = b != 0 && b < least_normal;
        if (state->mxcsr & PF_MXCSR_DAZ) {
            if (denormal_a) a = 0;
            if (denormal_b) b = 0;
        } else if (denormal_a || denormal_b) {
            raised = PF_MXCSR_DENORMAL;
        }
        bool negative_a = (lhs & sign) != 0, negative_b = (rhs & sign) != 0;
        if ((!a && !b) || (a == b && negative_a == negative_b)) {
            flags = 0x40; // ZF
        } else if (negative_a != negative_b) {
            flags = negative_a; // CF
        } else {
            flags = negative_a ? a > b : a < b;
        }
    }
    state->mxcsr |= raised;
    if (raised & ~(state->mxcsr >> PF_MXCSR_MASK_SHIFT)) {
        state->simd_fault = raised == PF_MXCSR_INVALID ? PF_SIMD_INVALID : PF_SIMD_DENORMAL;
        return false;
    }
    state->rflags = (pf_read_flags(state) & ~PF_ARITHMETIC_FLAGS) | flags;
    state->pending.operation = PF_NO_FLAGS;
    return true;
}

static inline bool pf_sse_compare_memory(struct pf_state *state, unsigned lhs, uint64_t address,
                                         unsigned width, bool ordered) {
    uint8_t *bytes;
    if (!pf_guest_span(state, address, width / 8, 1, false, &bytes)) return false;
    uint64_t rhs = width == 32 ? pf_read_le32(bytes) : pf_read_le64(bytes);
    return pf_sse_compare(state, state->xmm[lhs].qword[0], rhs, width, ordered);
}
