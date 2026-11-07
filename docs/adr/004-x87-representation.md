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
