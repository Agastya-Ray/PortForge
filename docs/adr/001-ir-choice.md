# ADR 001 Intermediate Representation

Status is accepted for the production C intermediate representation.

Choose the in-house PFIR and retain Remill with LLVM-CBE as a comparison
backend. PFIR carries guest register writes, checked memory operations,
pending integer flags, and explicit 80-bit x87 state through C emission.
The existing PE execution path already uses it. This decision establishes
that interface for subsequent milestones.

The comparison workflow validates native x86 results against ARM64 for
both candidates' integer and SSE2 slices. It also reruns PFIR's x87
arithmetic and stack corpus from the same revision. The evidence checker
rejects missing native references, mismatched digests, insufficient
samples, and incomparable timing workloads before writing
`ir-decision.json`. The ARM64 artifact contains that report alongside
generated C sizes and the three timing samples per candidate.

The current Remill-to-C x87 experiment uses host `long double`. The
ARM64 probe records 113 mantissa bits, while the guest x87 format has
64. A production Remill path would need explicit guest-format operations
and the same hardware acceptance tests. The decision does not assert a
production speed advantage from the adapter-level timings. The broader
M1 program corpus and full instruction coverage remain open.

Use the same guest bytes, seed, initial state and native x86 results for the
in-house PFIR and Remill/LLVM experiments. Compare defined flags, guest register
writes and the next instruction address. Extend the common corpus to cover x87
and SSE2. Compare emitted C size and execution time on ARM64 only after the
outputs match. Measure both candidates with the same C compiler and flags.

The in-house experiment begins with register-direct ADD, SUB and CMP in x86-64
mode. It represents each register read and arithmetic result as a distinct SSA
value, leaves flags pending until observed, and emits C11. The spike uses the Zydis decoder described in
[ADR 003](003-x86-decoder.md). The production path keeps this explicit state model.

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

## SSE2 register comparison

Start with legacy XMM PXOR, PADDD, PADDQ, PSUBD, and PSUBQ. Both candidates
receive the same instruction bytes and initial XMM state. Compare the
destination, the rest of the represented state, and the next RIP with an
x86 hardware reference. PFIR keeps XMM values as two unsigned 64-bit words.
It emits NEON on ARM64 and unsigned C operations on other hosts. Dword
arithmetic wraps separately in each 32-bit lane.

This slice covers register operands. Memory accesses, MMX, AVX, and
floating-point SSE2 have separate semantics and are outside these tests.

The x87 comparison remains separate. The Remill to LLVM-CBE experiment
lowered x87 arithmetic to host `long double`, which does not preserve the
guest 80-bit representation on ARM64. The x87 experiment must test explicit
80-bit storage and operations against native x86 before comparing performance.
