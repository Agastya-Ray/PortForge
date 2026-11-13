# ADR 021 Exact x87 extended memory transfers

FLD reads ten guest bytes in the explicit 80-bit representation. FSTP
stores the represented value and pops the stack. Loads accept canonical
finite values. Stores preserve the represented bits, including indefinite
values produced by masked stack faults.

Preflight the full memory span before modifying stack or memory. The
native corpus checks loads, stores and round trips across all stack depths,
signed zeros, denormals, precision controls and unaligned addresses.
ARM64 must match the native state, output bytes and execution digest.
