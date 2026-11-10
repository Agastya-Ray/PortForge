# ADR 012 Calls through guest function pointers

The PE translator discovers code from its normal entry point and from
any `--seed-rva 0x...` options. Each seed is an address relative to the
image base. A seed must point to file-backed executable bytes and can
never overlap another decoded instruction. Discovery keeps its existing
instruction budget and processes duplicate or reordered seeds identically.

Calls through a register or a memory operand can target an instruction
already translated. The guest stack receives the return address before
the dispatcher transfers control. A memory operand is read in full
before the stack changes. A pointer inside the image that does not name
a translated instruction stops at the original call. Outside the image,
the existing registered-import checks still apply. Direct calls through
the import table keep their existing binding rules.

The native test uses a Windows-format executable whose two target
functions are reachable only through pointers. The same code runs on
x86-64 hardware and in the translated runner. The test covers register
and stack-held pointers, malformed seeds, and memory faults that must
leave the caller's stack unchanged. CI repeats the translation and
compares its output and execution digest on ARM64.

This slice requires a discovered target. Automatic discovery of arbitrary
function pointers, indirect jumps, and runtime-generated code are later
work.
