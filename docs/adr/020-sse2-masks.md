# ADR 020 SSE2 masks and comparisons

Add PAND, PANDN, POR, PCMPEQD and PCMPGTD to the existing packed
operation interface. PANDN complements its original destination.
Dword comparisons produce full-lane masks. Portable signed comparison
uses sign-biased unsigned ordering. ARM64 uses corresponding NEON
operations.

The shared PFIR and Remill corpus covers ten operations with ordinary,
extended and aliased registers. Every encoding receives one million
seeded inputs. Native x86, NEON and portable ARM64 execution must agree.
