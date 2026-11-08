# ADR 005 Bounded Guest Memory

Status: Accepted for the M1 instruction experiment.

Represent memory as a contiguous file-independent byte span at a guest base
address. Validate the whole access before exposing its bytes. Distinguish
noncanonical addresses, alignment, unmapped ranges, and read-only stores.
Keep guest byte order explicit. The span models neither Windows page tables
nor segmentation. The M1 long-mode experiment uses 48-bit canonical addresses.

PFIR computes effective addresses before any access. Address-size overrides
truncate to 32 bits, RIP-relative operands use the next instruction address,
and signed displacements participate in unsigned wrapping arithmetic. LEA
computes this value without reading memory or changing flags.

Legacy SSE2 packed memory arithmetic uses a checked 16-byte aligned read
before committing the XMM destination. Explicit FS and GS memory operands
remain outside this flat-address experiment. A fault records the effective
address and leaves RIP at the faulting instruction.

MOVDQA requires 16-byte alignment. MOVDQU permits byte alignment. Both
validate the full span before changing memory or the XMM destination.
