
#include <stdlib.h>
#include <string.h>

enum pf_pe_status {
    PF_PE_RETURNED,
    PF_PE_UNKNOWN_RIP,
    PF_PE_BUDGET,
    PF_PE_MEMORY_FAULT,
    PF_PE_X87_UNSUPPORTED,
    PF_PE_UNBOUND_IMPORT,
    PF_PE_UNSUPPORTED_IMPORT,
    PF_PE_EXITED,
    PF_PE_CONTINUE,
    PF_PE_INTEGER_FAULT,
};

struct pf_pe_import {
    uint64_t slot;
    const char *dll;
    const char *name;
    uint16_t ordinal;
    bool exits;
};

typedef uint64_t (*pf_pe_host_fn)(void *, uint64_t, uint64_t, uint64_t, uint64_t);

struct pf_pe_guest_arg {
    size_t size;
    bool writable;
    unsigned size_arg; /* One-based argument containing a Win32 DWORD length. */
};

struct pf_pe_binding {
    uint64_t target;
    pf_pe_host_fn invoke;
    void *context;
    struct pf_pe_guest_arg args[4];
    bool null_fifth;
    bool exits;
};

struct pf_pe_export {
    const char *dll;
    const char *name;
    uint16_t ordinal;
    pf_pe_host_fn invoke;
    void *context;
    struct pf_pe_guest_arg args[4];
    bool null_fifth;
    bool exits;
};

enum pf_pe_bind_status {
    PF_PE_BIND_OK,
    PF_PE_BIND_INVALID,
    PF_PE_BIND_TARGET_RANGE,
    PF_PE_BIND_UNRESOLVED,
    PF_PE_BIND_AMBIGUOUS,
    PF_PE_BIND_MEMORY_FAULT,
    PF_PE_BIND_NO_MEMORY,
};

struct pf_pe_bind_result {
    enum pf_pe_bind_status status;
    size_t import_index;
    enum pf_memory_fault memory_fault;
};

extern const size_t pf_pe_import_count;
extern const struct pf_pe_import pf_pe_imports[];

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "import arguments must hold host pointers");

static inline unsigned char pf_pe_fold_ascii(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static inline bool pf_pe_same_dll(const char *a, const char *b) {
    while (*a && pf_pe_fold_ascii((unsigned char)*a) == pf_pe_fold_ascii((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static inline bool pf_pe_matches_export(const struct pf_pe_import *import,
                                        const struct pf_pe_export *export) {
    if (!pf_pe_same_dll(import->dll, export->dll)) return false;
    if (import->name && export->name) return strcmp(import->name, export->name) == 0;
    return !import->name && !export->name && import->ordinal == export->ordinal;
}

static inline bool pf_pe_token_range(const struct pf_memory *memory, uint64_t first, size_t count) {
    if (!first || first % 8 || count - 1 > (UINT64_MAX - first) / 8) return false;
    uint64_t last = first + (count - 1) * UINT64_C(8);
    if (!pf_canonical48(first) || !pf_canonical48(last) || ((first ^ last) >> 63)) return false;
    return !memory->size || last < memory->base
        || (first >= memory->base && first - memory->base >= memory->size);
}

struct pf_pe_bind_result pf_pe_bind_imports(struct pf_state *state, const struct pf_pe_export *exports,
                                             size_t export_count, uint64_t first_target,
                                             struct pf_pe_binding *out, size_t capacity) {
    size_t count = pf_pe_import_count;
    struct pf_pe_bind_result result = {PF_PE_BIND_OK, SIZE_MAX, PF_MEM_OK};
    if (!count) return result;
    if (!state || !out || capacity < count || (!exports && export_count)) {
        return (struct pf_pe_bind_result){PF_PE_BIND_INVALID, SIZE_MAX, PF_MEM_OK};
    }
    for (size_t i = 0; i < export_count; ++i) {
        if (!exports[i].dll || !exports[i].invoke) {
            return (struct pf_pe_bind_result){PF_PE_BIND_INVALID, SIZE_MAX, PF_MEM_OK};
        }
        for (size_t j = 0; j < 4; ++j) {
            struct pf_pe_guest_arg arg = exports[i].args[j];
            if (arg.size_arg > 4 || (arg.size_arg && arg.size)) {
                return (struct pf_pe_bind_result){PF_PE_BIND_INVALID, SIZE_MAX, PF_MEM_OK};
            }
        }
    }
    if (!pf_pe_token_range(&state->memory, first_target, count)) {
        return (struct pf_pe_bind_result){PF_PE_BIND_TARGET_RANGE, SIZE_MAX, PF_MEM_OK};
    }
    struct pf_pe_staged_import {
        struct pf_pe_binding binding;
        uint8_t *slot;
    };
    struct pf_pe_staged_import *staged = calloc(count, sizeof(*staged));
    if (!staged) return (struct pf_pe_bind_result){PF_PE_BIND_NO_MEMORY, SIZE_MAX, PF_MEM_OK};

    for (size_t i = 0; i < count; ++i) {
        const struct pf_pe_export *match = NULL;
        for (size_t j = 0; j < export_count; ++j) {
            if (!pf_pe_matches_export(&pf_pe_imports[i], &exports[j])) continue;
            if (match) {
                result = (struct pf_pe_bind_result){PF_PE_BIND_AMBIGUOUS, i, PF_MEM_OK};
                goto done;
            }
            match = &exports[j];
        }
        if (!match) {
            result = (struct pf_pe_bind_result){PF_PE_BIND_UNRESOLVED, i, PF_MEM_OK};
            goto done;
        }
        if (pf_pe_imports[i].exits && !match->exits) {
            result = (struct pf_pe_bind_result){PF_PE_BIND_INVALID, i, PF_MEM_OK};
            goto done;
        }
        enum pf_memory_fault fault = pf_memory_span(
            &state->memory, pf_pe_imports[i].slot, 8, 1, true, &staged[i].slot
        );
        if (fault != PF_MEM_OK) {
            result = (struct pf_pe_bind_result){PF_PE_BIND_MEMORY_FAULT, i, fault};
            goto done;
        }
        staged[i].binding = (struct pf_pe_binding){
            .target = first_target + i * UINT64_C(8), .invoke = match->invoke,
            .context = match->context, .null_fifth = match->null_fifth, .exits = match->exits,
        };
        memcpy(staged[i].binding.args, match->args, sizeof(staged[i].binding.args));
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = staged[i].binding;
        pf_write_le64(staged[i].slot, staged[i].binding.target);
    }
done:
    free(staged);
    return result;
}

static inline void pf_pe_call(struct pf_state *state, uint64_t target, uint64_t return_rip) {
    if (pf_push(state, return_rip)) state->rip = target;
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

static inline enum pf_pe_status pf_pe_invoke(struct pf_state *state, uint64_t target,
                                              const struct pf_pe_binding *binding, uint64_t return_rip) {
    uint8_t *bytes;
    if (!target || !binding || !binding->invoke || binding->target != target) return PF_PE_UNBOUND_IMPORT;
    if (binding->null_fifth) {
        /* The fifth argument follows the 32-byte register home area before CALL. */
        if (!pf_guest_span(state, state->gpr[4] + 32, 8, 1, false, &bytes)) return PF_PE_MEMORY_FAULT;
        if (pf_read_le64(bytes)) return PF_PE_UNSUPPORTED_IMPORT;
    }
    uint64_t args[] = {state->gpr[1], state->gpr[2], state->gpr[8], state->gpr[9]};
    uint64_t host_args[4];
    for (size_t i = 0; i < 4; ++i) {
        struct pf_pe_guest_arg arg = binding->args[i];
        if (arg.size_arg > 4) return PF_PE_UNSUPPORTED_IMPORT;
        size_t width = arg.size_arg ? (uint32_t)args[arg.size_arg - 1] : arg.size;
        if (!width) {
            host_args[i] = arg.size_arg ? 0 : args[i];
            continue;
        }
        if (!args[i]) {
            state->memory_fault = PF_MEM_UNMAPPED;
            state->fault_address = 0;
            return PF_PE_MEMORY_FAULT;
        }
        if (!pf_guest_span(state, args[i], width, 1, arg.writable, &bytes)) return PF_PE_MEMORY_FAULT;
        host_args[i] = (uintptr_t)bytes;
    }
    pf_pe_call(state, target, return_rip);
    if (state->memory_fault != PF_MEM_OK) return PF_PE_MEMORY_FAULT;
    uint64_t result = binding->invoke(binding->context, host_args[0], host_args[1], host_args[2], host_args[3]);
    if (binding->exits) {
        state->pe_exited = true;
        state->pe_exit_code = (uint32_t)result;
        return PF_PE_EXITED;
    }
    state->gpr[0] = result;
    pf_pe_return(state);
    return state->memory_fault == PF_MEM_OK ? PF_PE_CONTINUE : PF_PE_MEMORY_FAULT;
}

static inline enum pf_pe_status pf_pe_import_call(struct pf_state *state, uint64_t slot,
                                                  const struct pf_pe_binding *binding, uint64_t return_rip) {
    uint8_t *bytes;
    if (!pf_guest_span(state, slot, 8, 1, false, &bytes)) return PF_PE_MEMORY_FAULT;
    uint64_t target = pf_read_le64(bytes);
    if (!pf_pe_check_target(state, target)) return PF_PE_MEMORY_FAULT;
    return pf_pe_invoke(state, target, binding, return_rip);
}

static inline enum pf_pe_status pf_pe_register_call(struct pf_state *state, uint64_t target,
                                                    const struct pf_pe_binding *bindings,
                                                    size_t count, uint64_t return_rip) {
    if (!pf_pe_check_target(state, target)) return PF_PE_MEMORY_FAULT;
    for (size_t i = 0; bindings && i < count && i < pf_pe_import_count; ++i) {
        if (bindings[i].target == target) return pf_pe_invoke(state, target, &bindings[i], return_rip);
    }
    return PF_PE_UNBOUND_IMPORT;
}
