# ADR 010 Wine time exports

Status: Accepted for M1.

Run generated PE code inside a Winelib host process. Resolve
CompareFileTime and FileTimeToSystemTime from the system kernel32
module and expose them to the PE import binder through typed wrappers.
The wrappers convert checked guest byte spans into aligned host
structures and serialize successful results back to guest memory.

Each binding carries its own host context. Keep the Wine export table
at a stable address until all bindings derived from it are finished,
then release the DLL handle. The guest sees only its import tokens.

Use fixed FILETIME inputs to compare translated guest calls with direct
Win32 calls inside Wine on x86-64 and ARM64. Record the generated C and
the output digest as CI evidence.

The ARM64 runner uses Debian 13 Wine because Ubuntu 24.04's ARM64
Wine package hangs at startup. See [Ubuntu #2102681](https://bugs.launchpad.net/ubuntu/+source/wine/+bug/2102681)
and [Debian #1100695](https://bugs.debian.org/1100695).

This slice covers two time conversion exports from kernel32. A
general Windows API surface, arbitrary DLLs, and game execution
remain separate work.
