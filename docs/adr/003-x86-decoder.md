# ADR 003 X86 Instruction Decoder

Status: Accepted for the instruction decoder.

Use Zydis through its Rust bindings to identify instruction boundaries and
operands in Windows x86 and x64 code. The decoder is independent of ADR-001.
A decoded instruction can still require unsupported execution semantics.
The lifter reports that distinction explicitly.

Pin the binding to 4.1.1 and commit the Cargo lockfile. The Rust bindings,
Zydis and its Zycore dependency use MIT licenses. Fetch the locked sources
before offline compilation. Link only the full decoder, leaving formatter
and encoder features disabled.

[Zydis Rust bindings](https://github.com/zyantific/zydis-rs)
[Zydis](https://github.com/zyantific/zydis)
[Zycore](https://github.com/zyantific/zycore-c)
