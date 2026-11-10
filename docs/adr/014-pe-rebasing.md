# ADR 014 Run PE executables at a selected base

The `--image-base` option selects a nonzero, 64 KiB-aligned canonical
guest address. Pass the same option to `--pe` and `--pe-image` so the
emitted translation and mapped data use the same addresses. Without
the option, both commands retain their preferred-base behavior.

When the base changes, the existing loader applies the PE relocation
table to a bounded copy of the image before instruction discovery.
Discovery reads instruction bytes from that copy and still uses the
original file-backed executable boundaries. The emitter uses the selected
base for the entry point, import slots, branch targets, and dispatch.

The fixture compares an absolute address in an instruction with an
absolute pointer stored in data. It dereferences the result at the
preferred base and at addresses above and below it. CI repeats the
mapping and translation on x86-64 and ARM64 and compares their outputs.
