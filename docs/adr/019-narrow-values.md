# ADR 019 Narrow values and condition results

MOVZX, MOVSX and MOVSXD read byte, word and dword sources into 32-bit
or 64-bit registers. Track the enclosing register and high-byte offset
explicitly. Memory sources are checked before destination mutation.
Unsigned sign extension gives defined C behavior for negative values.

SETcc writes one byte in a register or checked memory while preserving
other bits and flags. Native tests cover all conditions, high-byte aliases,
SPL, extended registers, signed boundaries and partial memory faults.
