# ADR 024 Byte and word arithmetic and scalar memory writes

M1 must translate ordinary compiler operations on byte and word data, not
just zero/sign-extension into larger registers. Add MOV, integer arithmetic,
logic, comparisons and unary operations at 8 and 16 bits, read/modify/write
memory destinations at all four widths, and CMOV from memory at 16/32/64 bits.
The existing 32/64-bit register path remains unchanged.

Reference: Intel Software Developer Manual volume 2, the corresponding MOV,
ADD/ADC/SUB/SBB, CMP/TEST, AND/OR/XOR, INC/DEC/NEG/NOT and CMOVcc entries:
https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

Represent a register with its enclosing 64-bit register and byte offset.
Byte/word writes preserve the other bits; 32-bit writes clear the upper half.
This handles AH/CH/DH/BH and the different low-byte aliases selected by REX.
Arithmetic wraps using unsigned C and a mask before installing pending flags.
INC/DEC retain carry; MOV/NOT preserve the entire pending-flag calculation.

Check the complete memory span and write permission before any data or flags
change. Evaluate effective addresses using the old register state. CMOV fetches
its memory source even when the condition is false. LOCK-prefixed operations
remain rejected: single-threaded memory checks do not implement atomic x86.
FS/GS addressing and implicit segment behavior are also outside this slice.

The corpus executes the exact instruction bytes on native x86 with register,
flag and memory inputs, then compares emitted-C results. Each form gets one
million seeded cases; the same results must match on ARM64. Tests include
high-byte aliases, both carries, all conditions, truncated/read-only spans,
partial writes, and flag preservation. A compiler-built byte/word fixture is
also checked against fixed output with all three Windows compilers.
