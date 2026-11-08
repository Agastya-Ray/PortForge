# ADR 007 Scalar import thunks

Status: Accepted for the M1 import call experiment.

Recognize near PE32+ CALL instructions through RIP-relative memory only
when that address identifies an eager import address table (IAT) slot.
Require the full IAT and its terminator to be file-backed.
Emit a table of the referenced slots with their DLL and name or ordinal.
Sort the table by slot RVA. The host supplies one binding per emitted
entry and writes its nonzero guest target token into the mapped IAT.

Read the slot at execution time and require the token to match the
binding. A missing or changed binding stops at the CALL. Validate the
return-address push before invoking the host. Pass the four integer
argument registers RCX, RDX, R8 and R9 to a typed native C function and
write its result to RAX. Finish the call by restoring the stack pointer
and advancing RIP to the guest return address. The scalar host interface
marshals integer values only.

The first harness binds two controlled functions and compares native
x86-64 execution of the same executable with the translated code on
both architectures. This interface covers fixed integer signatures up
to four arguments. Wine symbol resolution, floating-point and stack
arguments, delay loads, pointer marshalling and callbacks into guest
code remain separate work.

- [Microsoft PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
