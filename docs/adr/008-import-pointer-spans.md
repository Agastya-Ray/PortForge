# ADR 008 Import pointer spans

Status: Accepted for M1.

A host binding can declare a fixed byte width and write permission for
any of its four integer arguments. A width of zero retains the scalar
behavior from ADR 007. Nonzero widths map nonnull guest addresses to
host byte pointers. Readable spans and writable spans are checked before
the CALL commits or the host function runs.

Pass a direct view of the checked bytes so overlapping arguments retain
their original aliasing. The host wrapper can use the view for the
duration of the call. It must respect the declared extent and permission,
use byte-safe access, and never retain or return a host pointer. The
guest argument registers still contain guest addresses.

Use the guest return-address slot after the host invocation, including
when a writable argument overlaps it. A bad return address faults after
the host effects have occurred. A preflight fault leaves the call state
and buffers untouched.

This slice supports fixed, nonnull spans. Variable lengths, nullable
arguments, nested pointers, and host pointer results need separate
signature support.
