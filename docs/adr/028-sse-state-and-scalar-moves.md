# ADR-028: Keep guest MXCSR and scalar SSE moves in PFIR

## Context

Compiler-built Windows x64 numerical programs use scalar SSE moves and the
MXCSR control/status register. The host ARM64 floating-point control register
has a different layout and belongs to the host thread. The translated guest
needs independent state.

## Decision

Store MXCSR in each PFIR CPU state and initialize the Wine entry point with
the Windows x64 reset value, 0x1f80. The guest profile exposes all 16 defined
MXCSR bits, including denormals-are-zero. LDMXCSR and STMXCSR use checked,
unaligned four-byte memory transfers. A reserved bit in a fully readable
load gives the PE dispatcher a SIMD fault with the failing RIP and leaves
the old MXCSR intact.

Treat legacy MOVSS and MOVSD as bit transfers. Register copies preserve
upper bits; memory loads clear upper bits and stores write only the low
four or eight bytes. These instructions leave x87 state, MXCSR and integer
flags untouched. AVX encodings and FS/GS-relative accesses remain outside
this change.

## Evidence

The M1 Guest Memory workflow runs the same deterministic corpus on x86-64
and ARM64. On x86-64 the corpus exercises exact instruction encodings for
the scalar moves and checks MXCSR round trips against hardware. Each form
runs one million seeded cases. The corpus also checks truncated and
read-only spans, address wrap, the high XMM registers, and all sixteen
reserved MXCSR bits. An invalid load stops its translated block before the
next instruction executes.
