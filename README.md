# PortForge

Here to help people relive nostalgia and play their most loved games on phone.

PortForge is a deterministic Windows game to Android porting pipeline under development.
The current milestone is M0 foundations. Use Rust 1.85.1 and Python 3.11 or later.

```sh
cargo build --release --locked --offline
target/release/pf detect /path/to/pc-game --out ./evidence
python3 corpus/check_detect.py target/release/pf ./test-evidence
```

Detection reads the game directory and writes `manifest.json` and `report.json`.
The manifest lists PE modules, their imports, and evidence for engine, graphics,
and input APIs. The report records malformed or unsupported executable files.
An empty manifest produces a nonzero exit code.

See [Architecture](docs/ARCHITECTURE.md) for the file interface and milestone status.
