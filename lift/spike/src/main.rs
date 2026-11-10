mod address;
mod pfir;
mod program;

use std::fmt::Write;
use std::io::{self, Write as _};
use std::{env, fs, process::ExitCode};

fn compile(mut arguments: impl Iterator<Item = String>) -> Result<String, String> {
    let mut source = include_str!("../../../runtime/guest/x87.h").to_owned();
    source.push('\n');
    source.push_str(include_str!("../../../runtime/guest/memory.h"));
    source.push('\n');
    source.push_str(include_str!("guest.h"));
    let Some(first) = arguments.next() else {
        return Err(
            "usage: pf-ir-spike <hex-block>... | --pe <file> [--image-base <0xVA>] [--seed-rva <0xRVA>]... | --pe-image <file> [--image-base <0xVA>]"
                .into(),
        );
    };
    if first == "--pe" {
        let path = arguments.next().ok_or("missing PE path after --pe")?;
        let (base, seeds) = pe_options(arguments, true)?;
        let bytes = fs::read(&path).map_err(|error| format!("{path}: {error}"))?;
        program::emit(&bytes, &seeds, base, &mut source)
            .map_err(|error| format!("{path}: {error}"))?;
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
    let mut arguments = env::args().skip(1).peekable();
    let output = if arguments
        .peek()
        .is_some_and(|option| option == "--pe-image")
    {
        arguments.next();
        map_image(arguments)
    } else {
        compile(arguments).map(String::into_bytes)
    };
    match output.and_then(|bytes| {
        io::stdout()
            .lock()
            .write_all(&bytes)
            .map_err(|error| error.to_string())
    }) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("pf-ir-spike: {error}");
            ExitCode::FAILURE
        }
    }
}

fn map_image(mut arguments: impl Iterator<Item = String>) -> Result<Vec<u8>, String> {
    let path = arguments.next().ok_or("missing PE path after --pe-image")?;
    let (base, _) = pe_options(arguments, false)?;
    let bytes = fs::read(&path).map_err(|error| format!("{path}: {error}"))?;
    program::map(&bytes, base).map_err(|error| format!("{path}: {error}"))
}

fn pe_options(
    mut arguments: impl Iterator<Item = String>,
    allow_seeds: bool,
) -> Result<(Option<u64>, Vec<u32>), String> {
    let mut base = None;
    let mut seeds = Vec::new();
    while let Some(option) = arguments.next() {
        let value = arguments
            .next()
            .ok_or_else(|| format!("missing value after {option}"))?;
        let address = value
            .strip_prefix("0x")
            .filter(|digits| digits.bytes().all(|byte| byte.is_ascii_hexdigit()))
            .and_then(|digits| u64::from_str_radix(digits, 16).ok())
            .ok_or_else(|| format!("invalid {option} {value:?}; expected a hexadecimal address"))?;
        match option.as_str() {
            "--image-base" if base.is_none() => base = Some(address),
            "--seed-rva" if allow_seeds && seeds.len() < 4096 => {
                seeds.push(
                    u32::try_from(address)
                        .map_err(|_| format!("seed RVA out of range: {value}"))?,
                );
            }
            _ => return Err(format!("unexpected or repeated PE option {option}")),
        }
    }
    Ok((base, seeds))
}
