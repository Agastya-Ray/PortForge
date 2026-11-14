# ADR 027 x87 numerical programs

M1 requires instruction evidence and compiler-built programs through ARM64
Wine. Standalone x87 tests alone do not prove the PE runner integration.

Initialize guest x87 state to the Windows x64 ABI control word, 0x027f.
FNINIT separately resets the architecture to 0x037f. Numerical helper calls
preserve the caller control word, including after an explicit reset.
Link the existing pinned, thread-local SoftFloat backend into every translated
console runner. Rebuild it for the actual host ABI; the ARM64 Wine runner uses
the Windows target ABI and cannot reuse a Linux object archive.

Add finite-input FDIV/FDIVR and their popping forms, and the control/status
instructions FNINIT, FNCLEX, FLDCW, FNSTCW and FNSTSW. Preserve reserved control
bits as x86 does. No-wait status/control instructions remain usable when an
exception is pending. FLDCW records a newly unmasked pending exception for
the next waiting instruction. Guest exception dispatch remains a separate
M1 requirement.

Extend native instruction fuzzing across precision and rounding modes,
division by zero, zero divided by zero, subnormal operands, stack faults,
and fault-before-mutation memory behavior. Add one numerical console program
built with Clang, MinGW and MSVC. Its small x87 assembly helper is necessary
because MSVC's x64 C compiler uses SSE2 rather than x87. Both assembly dialects
implement the same ABI and are checked against an independent rational-number
oracle. Compare exact output, control/status words and exit codes on native
Windows, x86 Wine and translated ARM64 Wine. Keep emitted files and guest
executables reproducible.

This unit does not claim support for NaN/Inf operands, every x87 opcode,
unmasked exception dispatch, threads or complete M1 acceptance.

References: [Windows x64 floating-point calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention#fpcsr),
[Intel instruction reference, volume 2](https://cdrdv2-public.intel.com/782156/325383-sdm-vol-2abcd.pdf).
