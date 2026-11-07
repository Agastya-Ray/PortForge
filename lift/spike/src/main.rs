mod pfir;

use std::fmt::Write;
use std::{env, process::ExitCode};

fn compile(arguments: impl Iterator<Item = String>) -> Result<String, String> {
    let mut source = include_str!("guest.h").to_owned();
    let mut count = 0;
    for hex in arguments {
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
    if count == 0 {
        return Err("usage: pf-ir-spike <hex-block> [<hex-block> ...]".into());
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
