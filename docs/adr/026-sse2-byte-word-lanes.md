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

The expanded Remill comparison exposed a pinned LLVM-CBE generator bug:
unsigned minima were emitted as invalid identifiers such as UCHAR0 and
USHRT0. Apply a strict, idempotent source patch before building that tool;
unsigned minima must be the literal zero. Do not rewrite emitted game C
or relax compilation checks. A small LLVM-intrinsic regression compiles and
executes scalar/vector unsigned saturating subtraction at 8/16/32/64 bits,
including exhaustive byte inputs, before the rebuilt generator is used.

After that repair, linking exposed missing PSUBSB/PSUBSW XMM definitions
in the pinned Remill revision. Add these reference semantics using widened
Remill signed operators and explicit lane bounds, with register/memory
selectors. Keep the shared native oracle and all 26-operation comparison
checks. An unexpected Remill hypercall aborts with its operation and RIP;
it never substitutes PFIR or silently accepts an unsupported reference.

The next native comparison isolated a further CBE defect: its vector storage
is unsigned, but arithmetic right shifts, signed comparisons and sign
extensions read those lanes without scalar signed casts. Emit the required
source/destination element casts, including all-ones extension of an i1 mask.
Extend the rebuilt-generator regression with arithmetic shifts, signed lane
comparisons and sign extensions at 8/16/32/64 bits and i1 masks. The tests
use independent unsigned bit formulas rather than the generator logic.
