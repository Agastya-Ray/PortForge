# ADR 025 Full-width integer products, division and sign extension

M1 needs x86's implicit two-register products and dividends. Add unsigned
MUL, one-operand IMUL, DIV and IDIV at 8/16/32/64 bits, plus CBW/CWDE/CDQE
and CWD/CDQ/CQO. Register and checked memory operands use the old register
state, including aliases of the implicit accumulator or high register.

Reference: Intel Software Developer Manual volume 2, MUL, IMUL, DIV, IDIV,
CBW/CWDE/CDQE and CWD/CDQ/CQO:
https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

Use two unsigned 64-bit limbs for 128-bit quantities. Multiplication uses
32-bit partial products; division uses bounded unsigned long division.
Signed operations take unsigned magnitudes, operate on those, then restore
the result sign. This avoids compiler-specific 128-bit types, signed C
overflow, and signed division of the minimum value by minus one.

Division truncates toward zero and gives the remainder the dividend sign.
Zero divisors and quotients outside the destination range record a guest
divide error before any register or flag changes. Preserve the instruction
address so diagnostics identify the failing instruction. Memory faults
have priority because the operand must be fetched first. The PE dispatcher
returns a distinct arithmetic-fault status; this records #DE but does not
implement Windows exception dispatch or guest exception handlers.

MUL/IMUL define carry and overflow; retain other flag values. DIV/IDIV retain
all architecturally undefined flags deterministically. Byte products use AX;
byte division writes AL and AH. Word writes preserve the other register bits,
and dword writes clear upper halves. Sign-extension instructions preserve flags.

Native x86 instruction oracles check valid products, quotients and remainders.
Fault tests check zero, overflow, unchanged state and PE dispatcher propagation.
A compiler-built division program is compared across Windows and translated
Wine using fixed expected output. No instruction-specific host signal handler
or change to host floating-point state is needed.
