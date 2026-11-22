# ADR-029: Compare scalar SSE values using guest floating-point state

## Decision

Lift legacy COMISS, UCOMISS, COMISD and UCOMISD with register and memory
sources. Compare the IEEE 754 encodings directly. This gives exact binary32
and binary64 ordering, signed-zero equality and NaN classification without
converting through the host floating-point environment.

Use MXCSR denormals-are-zero for signed zero substitution. Without that
setting, denormal inputs set the denormal status bit. NaNs take priority
over denormal operands. COMIS raises invalid for any NaN and UCOMIS raises
invalid for a signaling NaN. A masked exception sets the sticky bit and
allows the comparison to set ZF, PF and CF and clear OF, SF and AF. An
unmasked exception sets the sticky bit, reports a SIMD fault at the original
RIP and preserves the existing integer flags, including pending flags. A
memory source must pass the complete span check before either outcome.

## Evidence

The same eight encodings run one million inputs each on x86-64 and ARM64.
The x86-64 reference executes hardware comparison instructions while ARM64
repeats the inputs and compares the results with the x86-64 digest. The
program corpus also adds a Windows executable that checks the entry MXCSR,
NaNs, signed zero, denormal reporting, and denormals-are-zero, and compares
raw output with a fixed oracle.

This slice communicates precise faults to the PE dispatcher. Delivering
Windows structured exceptions and providing SSE arithmetic are separate
M1 work.
