# ADR 018 Scalar integer shifts

Support SHL, SHR and SAR on 32-bit and 64-bit registers with an immediate
count or CL. Mask the count before evaluation. Zero counts preserve pending
flags. Nonzero counts update defined carry, sign, zero and parity flags.
A count of one defines overflow. Other undefined flags retain their value.

The native corpus covers all eight-bit counts, sign boundaries, both
register widths, and immediate encodings. ARM64 repeats the same cases
and compares the execution digest. Memory destinations remain separate.
