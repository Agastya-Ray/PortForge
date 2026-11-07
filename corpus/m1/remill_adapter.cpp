#include <cstdint>
#include <cstdlib>
#include <remill/Arch/X86/Runtime/State.h>

#define PF_TRACE(index, symbol) extern "C" void *symbol(State *, uint64_t, void *);
#include "remill_entries.inc"
#undef PF_TRACE

using Trace = void *(*)(State *, uint64_t, void *);
static const Trace traces[] = {
#define PF_TRACE(index, symbol) symbol,
#include "remill_entries.inc"
#undef PF_TRACE
};

extern "C" bool __remill_flag_computation_zero(bool result, ...) { return result; }
extern "C" bool __remill_flag_computation_sign(bool result, ...) { return result; }
extern "C" bool __remill_flag_computation_overflow(bool result, ...) { return result; }
extern "C" bool __remill_flag_computation_carry(bool result, ...) { return result; }

extern "C" void *__remill_error(State *, uint64_t, void *) { std::abort(); }

static thread_local bool reached_boundary;
extern "C" void *__remill_missing_block(State *state, uint64_t address, void *memory) {
    if (reached_boundary || state->gpr.rip.qword != address) {
        std::abort();
    }
    reached_boundary = true;
    return memory;
}

#define PF_GPRS(X) \
    X(0, rax) X(1, rcx) X(2, rdx) X(3, rbx) \
    X(4, rsp) X(5, rbp) X(6, rsi) X(7, rdi) \
    X(8, r8) X(9, r9) X(10, r10) X(11, r11) \
    X(12, r12) X(13, r13) X(14, r14) X(15, r15)

// Remill stores these flags separately from the packed RFLAGS word.
#define PF_FLAG_BITS(X) X(0, cf) X(2, pf) X(4, af) X(6, zf) X(7, sf) X(10, df) X(11, of)

extern "C" void pf_remill_execute(unsigned index, uint64_t registers[16],
                                  uint64_t *rip, uint64_t *flags) {
    State state{};
#define PF_LOAD(index, name) state.gpr.name.qword = registers[index];
    PF_GPRS(PF_LOAD)
#undef PF_LOAD
    state.gpr.rip.qword = *rip;
    state.rflag.flat = *flags;
#define PF_LOAD_FLAG(bit, name) state.aflag.name = (*flags >> bit) & 1;
    PF_FLAG_BITS(PF_LOAD_FLAG)
#undef PF_LOAD_FLAG

    reached_boundary = false;
    if (index >= sizeof(traces) / sizeof(traces[0])
        || traces[index](&state, *rip, nullptr) != nullptr
        || !reached_boundary) {
        std::abort();
    }

#define PF_STORE(index, name) registers[index] = state.gpr.name.qword;
    PF_GPRS(PF_STORE)
#undef PF_STORE
    *rip = state.gpr.rip.qword;
    constexpr uint64_t separated_flags = UINT64_C(0xcd5);
    *flags = state.rflag.flat & ~separated_flags;
#define PF_STORE_FLAG(bit, name) *flags |= uint64_t(state.aflag.name) << bit;
    PF_FLAG_BITS(PF_STORE_FLAG)
#undef PF_STORE_FLAG
}
