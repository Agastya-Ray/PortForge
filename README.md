# PortForge

Here to help people relive nostalgia and play their most loved games on phone.

PortForge is a deterministic Windows game to Android porting pipeline under development.
M1 is in progress. Use Rust 1.85.1 and Python 3.11 or later.
Building the full workspace also requires CMake and a C compiler for Zydis.

```sh
cargo fetch --locked
cargo build -p pf --release --locked --offline
target/release/pf detect /path/to/pc-game --out ./evidence
python3 corpus/check_detect.py target/release/pf ./test-evidence
```

Detection reads the game directory and writes `manifest.json` and `report.json`.
The manifest lists PE modules, their imports, and evidence for engine, graphics,
and input APIs. The report records malformed or unsupported executable files.
An empty manifest produces a nonzero exit code.

To run the lifter experiment after fetching the locked sources:

```sh
bash corpus/m1/check_arithmetic.sh ./evidence
bash corpus/m1/check_pe_entry.sh ./evidence/pe-entry  # x86-64 Linux
bash corpus/m1/check_pe_console.sh ./evidence/console  # x86-64 Linux with Wine, Clang and LLD
bash corpus/m1/check_pe_console.sh ./evidence/mingw "" mingw  # x86-64 Linux with Wine and MinGW GCC
```

The console check builds Windows executables, translates them, and compares
their raw output and exit status against a native x86-64 run under Wine.
CI builds the same ten programs with Clang, MinGW GCC, and MSVC. The
MSVC job also records native Windows results. The ARM64 job translates
the original executables from each compiler and checks their output and
exit status. See [console execution](docs/adr/011-console-programs.md) for
the supported Wine calls and the runner's limits.

The [integer arithmetic corpus](docs/adr/023-integer-products-and-carry.md)
checks 44 instruction forms against native x86 execution and compares
their digest on ARM64. It includes multiplication, carry/borrow chains,
negation and bit inversion. Run it with:

```sh
bash corpus/m1/check_memory_ops.sh integer-products
```

See [Architecture](docs/ARCHITECTURE.md) for the file interface and milestone status.

Byte/word arithmetic and checked scalar memory writes are described in
[ADR-024](docs/adr/024-scalar-storage.md). Run their native/ARM64 corpus with
`bash corpus/m1/check_scalar_storage.sh`.

Full-width integer products, division and guest divide-error reporting are
described in [ADR-025](docs/adr/025-wide-integers.md). Run their corpus with
`bash corpus/m1/check_wide_integers.sh`.
