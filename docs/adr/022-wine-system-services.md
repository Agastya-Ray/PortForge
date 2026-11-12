# ADR 022 Process and system clock services

Expose the current Wine process and thread identifiers, GetTickCount64,
Sleep, and GetSystemTimeAsFileTime. Identifiers describe the active host
execution context. The FILETIME output span is checked before host work
and serialized in guest byte order. Wrappers isolate host and guest errors.

The compiler corpus checks stable nonzero identifiers, valid time, ticks
around Sleep, and exact success output on native Windows and both Wine
architectures. Raw identifiers and clocks vary between hosts.

Failed comparisons print bounded byte diagnostics. Boot logs and captured
streams are retained in artifacts even when a workflow fails.
