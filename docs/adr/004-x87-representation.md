# ADR 004 Portable x87 Arithmetic

Status: Proposed for the x87 experiment.

Store the guest value as an explicit significand and sign/exponent. Build
Berkeley SoftFloat from a pinned revision with thread-local exception and
rounding state. Set the guest precision and rounding controls per operation.
The x86 oracle loads and stores the guest format directly with x87 instructions.

For the first addition slice accept canonical finite inputs. Preserve the
explicit x87 denormal-operand flag and compare the rounded result against
round-toward-zero to determine whether the significand was incremented.
Reject unsupported encodings and the reserved precision control. Test result
bits and status against native x86 before using the helper in the lifter.

SoftFloat is a BSD-3-Clause test and runtime dependency. It is fetched at a
pinned revision rather than vendored.

[Berkeley SoftFloat interface](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)
[Intel 64 and IA-32 software developer manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

## Initial register stack

Represent the eight physical slots and occupancy separately. Derive TOP
from the x87 status word and map logical ST(i) through it. This slice covers
FLD1, FLDZ, FCHS, FABS, and register FADD/FADDP with all exception masks set.
Masked stack faults write the x87 indefinite value and update TOP and tags
where the instruction pushes or pops. Leave undefined condition bits alone.

The C emitter stops at an unsupported x87 instruction and reports its RIP
through the unchanged guest program counter. An unmasked control word,
pending exception, or unsupported arithmetic operand leaves the x87 state
unchanged. Deferred trap delivery, x87 instruction/data pointer metadata,
and full environment instructions remain separate work.
