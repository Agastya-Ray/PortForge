# ADR 017 SSE2 immediate packed shifts

Support legacy XMM PSLLD, PSLLQ, PSRLD, PSRLQ, and PSRAD with an
immediate count. Logical shifts zero lanes when the count reaches the
lane width. Arithmetic right shifts extend each dword's sign and saturate
the count at 31. The implementation uses unsigned C operations and avoids
undefined full-width shifts. Integer flags and other registers stay intact.

The corpus checks seven counts around the 32-bit and 64-bit boundaries,
including zero and 255. Every encoding receives one million inputs and
is compared with SSE2 hardware. ARM64 runs the same corpus and compares
its digest. Register-count, memory-count, MMX, and AVX encodings remain
separate work.
