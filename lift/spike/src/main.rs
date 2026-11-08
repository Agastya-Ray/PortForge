mod address;
mod pfir;
mod program;

use std::fmt::Write;
use std::{env, fs, process::ExitCode};

fn compile(mut arguments: impl Iterator<Item = String>) -> Result<String, String> {
    let mut source = include_str!("../../../runtime/guest/x87.h").to_owned();
    source.push('\n');
    source.push_str(include_str!("../../../runtime/guest/memory.h"));
    source.push('\n');
    source.push_str(include_str!("guest.h"));
    let Some(first) = arguments.next() else {
        return Err("usage: pf-ir-spike <hex-block> [<hex-block> ...] | --pe <file>".into());
    };
    if first == "--pe" {
        let path = arguments.next().ok_or("missing PE path after --pe")?;
        if arguments.next().is_some() {
            return Err("--pe expects exactly one input path".into());
        }
        let bytes = fs::read(&path).map_err(|error| format!("{path}: {error}"))?;
        program::emit(&bytes, &mut source).map_err(|error| format!("{path}: {error}"))?;
        return Ok(source);
    }
    let mut count = 0;
    for hex in std::iter::once(first).chain(arguments) {
        if hex.is_empty() || !hex.bytes().all(|byte| byte.is_ascii_hexdigit()) || hex.len() % 2 != 0
        {
            return Err(format!(
                "expected a nonempty hexadecimal block, got {hex:?}"
            ));
        }
        let bytes = (0..hex.len())
            .step_by(2)
            .map(|index| u8::from_str_radix(&hex[index..index + 2], 16))
            .collect::<Result<Vec<_>, _>>()
            .map_err(|_| format!("invalid hexadecimal block {hex:?}"))?;
        let block = pfir::lift(&bytes).map_err(|error| format!("{hex}: {error}"))?;
        writeln!(source, "\nvoid pf_block_{count}(struct pf_state *state) {{").unwrap();
        pfir::emit_c(&block, &mut source);
        writeln!(source, "}}").unwrap();
        count += 1;
    }
    Ok(source)
}

fn main() -> ExitCode {
    match compile(env::args().skip(1)) {
        Ok(source) => {
            print!("{source}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("pf-ir-spike: {error}");
            ExitCode::FAILURE
        }
    }
}
