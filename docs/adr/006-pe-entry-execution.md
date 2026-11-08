# ADR 006 PE entry execution experiment

Status: Accepted for M1 evidence. The production IR choice remains ADR 001.

Use a freestanding PE32+ fixture to exercise the PE reader, decoder, PFIR,
C emitter, and guest state together. Discover file-backed instructions
from the executable's entry RVA by following direct successors. Emit in
ascending RVA order, use the preferred image base, and cap discovery at
4,096 instructions. Report unsupported instructions, overlapping decoded
ranges, and missing executable targets with their RVAs.

The initial control flow is JMP, JZ, JNZ, CALL with a relative target, and
unprefixed near RET. JZ and JNZ read the existing lazy zero flag. CALL and
RET transfer through checked guest stack memory. A bounded dispatch loop
selects each translated instruction using RIP. The harness installs a return
address outside the image and supplies the caller's Windows x64 argument
registers. It compares against native execution of the same fixture code.

This M1 experiment supports static direct targets and near returns. Import
binding, broader discovery seeds, relocation-aware execution, and fallback
remain later work. The instruction budget is a host guard, independent of
guest state.

- [Intel instruction set reference](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
