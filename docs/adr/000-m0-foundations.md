# ADR 000 Rust Foundations

Accepted for the M0 implementation.

Use a Rust workspace with independent CLI and detection crates. Keep M0
dependency-free so its build works with an empty package registry. Read PE
structures with bounds-checked slices. Emit versioned JSON sorted by relative
path and rule identifier so equivalent input trees produce identical outputs.

Use the Microsoft PE/COFF file format as the parser reference.
[PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

Keep PE image mapping separate from file inspection. Detection reads the
file-backed bytes it needs and the loader materializes a guest image only
when called with a memory budget and base.
