# ADR 026 SSE2 byte and word arithmetic and saturation

Expand M1's packed integer path with PADDB/PADDW, PSUBB/PSUBW,
PADDSB/PADDSW, PADDUSB/PADDUSW, PSUBSB/PSUBSW, PSUBUSB/PSUBUSW,
PMULLW, PMULHW, PMULHUW and PMULUDQ. The first group wraps each lane;
the saturation groups clamp each signed or unsigned lane to its legal range.
Multiply forms retain low words, high signed/unsigned words, or full products
of the even-numbered unsigned dword lanes respectively.

Reference: Intel Software Developer Manual volume 2, PADD/PSUB and PMUL
instruction entries:
https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

ARM64 uses NEON wrapping, saturating and widening multiply instructions.
The portable path extracts lanes by unsigned shifts, uses bounded signed
32-bit intermediates for signed word products/saturation, then packs unsigned
bits. Avoid signed shifts, signed overflow, native-endian vector casts, and
implementation-defined unsigned-to-signed conversions. Saturation affects
only the result; preserve all guest flags and pending flags.

Keep this as an extension of the existing shared PFIR/Remill corpus rather
than an isolated backend test. Each operation gets one million inputs with
ordinary, extended and aliased register encodings, with native x86 oracles.
Add byte/word boundary patterns and compare portable versus NEON ARM64
results. Legacy 128-bit memory sources use checked aligned spans; faults
must precede XMM changes. MMX, VEX/EVEX and segmented forms stay rejected.
A compiler-built vector program adds a whole-program check with fixed output.

Compiler vector copies also use MOVAPS/MOVUPS and MOVAPD/MOVUPD. Accept
these as bit-preserving XMM move aliases with the same aligned/unaligned
checks as MOVDQA/MOVDQU, and fuzz all six move forms.

The fixture requests 16-byte global alignment with the native MSVC or
GCC/Clang spelling. This avoids silently ignored alignment attributes while
keeping one shared C source for all three compiler builds.
