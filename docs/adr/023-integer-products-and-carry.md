# ADR 023 Integer products and carry chains

M1 needs the integer arithmetic emitted by ordinary Windows compilers.
Add ADC (add with carry), SBB (subtract with borrow), NEG (negate), NOT
(invert bits), and the two- and three-operand forms of IMUL (multiply).
This slice accepts 32-bit and 64-bit register destinations. Binary sources
can be registers, checked memory, or the immediates allowed by the decoder.
One-operand multiply, byte/word destinations, memory destinations, and
atomic LOCK forms remain unsupported and must fail translation.

The reference is Intel's Software Developer Manual, volume 2, entries ADC,
SBB, NEG, NOT and IMUL:
https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

Use unsigned C arithmetic for wrapping results. ADC and SBB read the old
carry flag before replacing pending flags. Their new flags use the original
operands, the result, and the incoming carry, including a carry or borrow
when equal operands wrap around. NEG behaves as subtraction from zero;
NOT preserves all flags, including any pending calculation.

IMUL keeps the low destination-width bits and defines only carry and
overflow. Detect signed overflow using unsigned operand magnitudes and
the signed result limit; avoid signed C overflow and implementation-defined
unsigned-to-signed casts. Retain the old values of architecturally undefined
flags for deterministic execution. Three-operand IMUL reads its source
before writing its destination, including when registers alias.

Tests compare at least one million seeded inputs for each encoding against
native x86 instructions, then compare the same digest on ARM64. Include
signed boundaries, both incoming carries, pending-flag chains, immediate
sign extension, register aliases, and memory faults before state mutation.
A compiler-built console program exercises arithmetic in whole-program
translation with Clang, MinGW and MSVC through the existing Wine corpus.
