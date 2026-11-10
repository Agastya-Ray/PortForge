# ADR 016 Exact x87 signed integer loads

FILD reads a signed 16-, 32-, or 64-bit integer from checked guest memory
and converts it to the explicit 80-bit stack representation. The conversion
uses unsigned magnitude and normalization so the most negative integer is
handled without signed overflow. All supported integer values fit exactly
in the x87 significand, including the 64-bit minimum.

The corpus compares one million inputs for each encoding against x87
hardware. It varies precision and rounding controls, unaligned memory,
and all stack depths including masked stack overflow. It checks occupied
register bits, stack position, status, and fault paths that must preserve
the existing state. ARM64 runs the same corpus and matches the digest.
