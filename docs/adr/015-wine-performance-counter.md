# ADR 015 Windows performance counters through Wine

The guest can query Wine for its performance counter and frequency. The
import binder checks the eight-byte output buffer before either API runs.
The wrapper uses a host-local `LARGE_INTEGER`, writes its result in guest
byte order on success, and saves and restores the host thread's error
value while updating the guest's error value.

The console fixture tests positive, stable frequency and nondecreasing
counter readings with Clang, MinGW GCC, and MSVC. It checks the same
invariants on native Windows, x86-64 Wine, and translated ARM64 Wine.
The elapsed time and raw counter values depend on the host. The fixture
compares its success output and exit code across those runs.
