# PortForge Architecture

The Rust CLI runs detection first. Detection inventories the input folder, reads
PE32 and PE32+ modules, and writes manifest and report JSON through the CLI.
The eventual planner reads these files and selects one or more routes per game.

| Route | Planned implementation |
| --- | --- |
| Engine | Reuse an Android engine runtime with the original game data |
| Bytecode | Run portable bytecode on an ARM runtime |
| Native | Discover guest blocks, lift and optimize, emit C, and use a runtime translator for misses |

All routes share Android platform services, APK packaging, and replay verification.
The build order and acceptance gates are defined in the PortForge build spec.

## M0 file interface

`manifest.json` version 1 contains sorted relative module paths, machine
architecture, preferred image base, image size, entry RVA, imports, and engine
and API evidence. The importer lists name and ordinal entries, including
delay-loaded imports. Engine signatures describe files found and API evidence
describes imports observed. Late dynamic imports remain outside static evidence.

`report.json` version 1 records the module count, per-file parse failures, and
evidence for detector matches. Protection classification is outside detection
scope per the project owner's clarification.

The corpus checks deterministic JSON and expected PE facts on generated
fixtures. GitHub CI is configured for Linux x86-64 and ARM64 and uploads
the evidence and repeated release executable hashes.

## PE image loading

The loader maps headers and section data into a caller-budgeted guest image.
It fills virtual tails with zeros and applies x86 or x64 base relocations when
the caller selects a different base. Import binding and execution belong to
later milestones. Import tables, strings, and relocation directories can span
adjacent file-backed RVA regions. Reads reject gaps in the mapped file.

The CLI corpus verifies 20 generated files. A separate check compares packed and
unpacked Windows launchers with GNU objdump. It gets sample files from the local
pip installation and packs disposable copies. PortForge itself has no packer
dependency.
