# ADR 001 Intermediate Representation

Status: Proposed. Compare both implementations before choosing the production IR.

Use the same guest bytes, seed, initial state and native x86 results for the
in-house PFIR and Remill/LLVM experiments. Compare defined flags, guest register
writes and the next instruction address. Extend the common corpus to cover x87
and SSE2. Compare emitted C size and execution time on ARM64 only after the
outputs match. Measure both candidates with the same C compiler and flags.

The in-house experiment begins with register-direct ADD, SUB and CMP in x86-64
mode. It represents each register read and arithmetic result as a distinct SSA
value, leaves flags pending until observed, and emits C11. The spike uses the Zydis decoder described in
[ADR 003](003-x86-decoder.md). The production lifter remains subject to this decision.

The Remill experiment must lift the same byte sequences to LLVM IR and emit C
with equivalent state transitions. Record any operations the C backend cannot
express and test emitted C against the same native reference. Running Remill's
LLVM output alone would measure lifting but leave the C backend unevaluated.

References:

- [Intel 64 and IA-32 software developer manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Remill lift tool](https://github.com/lifting-bits/remill/blob/master/bin/lift/README.md)

## Arithmetic comparison

Both candidates run the same byte sequences and seed. The Remill candidate
uses Remill v6.0.1 and the LLVM 19-compatible LLVM-CBE revision pinned in
`corpus/m1/prepare_remill.sh`. Remill is Apache-2.0 and this LLVM-CBE revision
uses the University of Illinois/NCSA license. They are build tools for the
experiment. The project does not vendor them.

The comparison compiles Remill's emitted C in GNU C11 mode with
`uint128_t` mapped to the host compiler's `__uint128_t`. The experiment
supplies only the arithmetic marker and trace-boundary functions required
by these register-only traces. Unexpected control flow aborts.

The ARM64 CI job records generated C size and three execution timings per
backend. These timings include the test adapter and flag reads. The adapter
copies between two state layouts for each invocation, so these figures do
not estimate a production runtime that retains Remill's state across blocks.
