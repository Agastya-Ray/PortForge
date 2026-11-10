# ADR 011 Run Windows console programs

Status is accepted for the M1 console fixtures.

Compile four x86-64 Windows console programs with Clang and LLD, MinGW
GCC, and MSVC. The Windows entry point calls kernel32 directly. Clang
uses an import library generated from a definition file, and the other
two builds use their toolchain's import library. Wine runs the original
executables on x86-64 to establish reference output. The MSVC build also
runs on Windows and its output must match the Wine reference. PortForge
translates each original executable and runs the emitted C inside Wine
on x86-64 and ARM64.

The `--pe-image` command uses the existing PE loader to materialize the
preferred image with a 64 MiB allocation limit. The Wine console runner
places a 64 KiB stack after the image and sets up the Windows x64 register
home area and a return sentinel. The image and stack share one writable
guest memory region in this experiment. PE section permissions, command
line and environment setup, C runtime startup, and DLL initialization are
separate work.

The compiler generated register-indirect calls after reading an import
slot. For an executable with a discovered register-indirect CALL, emit
all eager import slots and resolve them through the host registry. A
register-indirect call accepts only a known import token. A copied token
keeps its meaning if the original slot is overwritten. General indirect
guest control flow remains outside this runner.

GetStdHandle exposes the standard output and error streams through opaque
guest tokens. WriteFile supports synchronous writes to those streams. The
runtime checks the complete input span using the DWORD byte count, the
writable DWORD result, and the null fifth argument in the caller's stack.
An invalid span or unsupported overlapped write stops before the host call.
The host writes the byte count back in little-endian order. ExitProcess
records the guest's full 32-bit exit code and ends guest execution so the
runner can release its resources. Discovery treats a direct call to this
import as terminal and the binder requires a terminating implementation.

The fixtures check text with mixed line endings, a computed binary payload,
embedded zero bytes, a zero-length write, a separate stderr message, and
a nonzero process exit status. The call fixture makes two calls to a
non-inlined C function with five arguments and compares the results it
writes. CI compares both streams as raw bytes and checks the status against
the native run and fixed expected results. It also checks repeated PE
builds, generated C, and mapped images. Fault tests verify that invalid
arguments leave guest memory and registers intact.

This covers the initial console fixtures across the three compilers.
The broader program corpus, general Win32 coverage, and the M1 IR
decision are still open.

- [Windows x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)
- [ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess)
