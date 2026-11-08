# ADR 005 Bounded Guest Memory

Status: Accepted for the M1 instruction experiment.

Represent memory as a contiguous file-independent byte span at a guest base
address. Validate the whole access before exposing its bytes. Distinguish
noncanonical addresses, alignment, unmapped ranges, and read-only stores.
Keep guest byte order explicit. The span models neither Windows page tables
nor segmentation. The M1 long-mode experiment uses 48-bit canonical addresses.
