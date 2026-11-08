# ADR 009 Host export registry

Status: Accepted for M1.

Resolve each emitted import against an explicit registry of host
functions and argument descriptors. Match DLL names with ASCII case
folding, match export names byte for byte, and distinguish names from
ordinals. An unresolved or ambiguous symbol reports its position in
the emitted import table.

The loader supplies a reserved guest token base. Assign successive
eight-byte-aligned tokens in emitted IAT order and reject a range that
overflows, crosses the canonical address gap, or intersects mapped
guest memory. The tokens identify bindings and are never host pointers.

Preflight symbol resolution and the complete writable IAT slots before
committing either the guest IAT or the caller's binding array. A failed
bind leaves both unchanged. Binding runs before guest execution or
under exclusive access to the state. The registry and output array are
host-owned and separate from the guest memory backing.

This registry takes already available host entry points. Loading Wine
modules and adapting their exported signatures remain separate.
