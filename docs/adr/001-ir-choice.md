# ADR 001 Intermediate Representation

Status: Proposed. Compare both implementations before choosing the production IR.

Use the same guest bytes, seed, initial state and native x86 results for the
in-house PFIR and Remill/LLVM experiments. Compare defined flags, guest register
writes and the next instruction address. Extend the common corpus to cover x87
and SSE2. Compare emitted C size and execution time on ARM64 only after the
outputs match. Measure both candidates with the same C compiler and flags.

The in-house experiment begins with register-direct ADD, SUB and CMP in x86-64
mode. It represents each register read and arithmetic result as a distinct SSA
value, leaves flags pending until observed, and emits C11. Its small decoder is
limited to the experiment's encodings. The production decoder and lifter remain
subject to the decision.

The Remill experiment must lift the same byte sequences to LLVM IR and emit C
with equivalent state transitions. Record any operations the C backend cannot
express and test emitted C against the same native reference. Running Remill's
LLVM output alone would measure lifting but leave the C backend unevaluated.

References:

- [Intel 64 and IA-32 software developer manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Remill lift tool](https://github.com/lifting-bits/remill/blob/master/bin/lift/README.md)
