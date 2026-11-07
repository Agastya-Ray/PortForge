use std::env;
use std::error::Error;
use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "Usage: pf detect <game-directory> [--out <output-directory>]";

fn run() -> Result<(), Box<dyn Error>> {
    let mut args = env::args_os().skip(1);
    let command = args.next();
    if command
        .as_deref()
        .is_some_and(|arg| arg == "--help" || arg == "-h")
    {
        println!("{USAGE}");
        return Ok(());
    }
    if !command.as_deref().is_some_and(|arg| arg == "detect") {
        return Err(USAGE.into());
    }
    let root = PathBuf::from(args.next().ok_or(USAGE)?);
    let output = match args.next() {
        None => env::current_dir()?,
        Some(arg) if arg == "--out" => PathBuf::from(args.next().ok_or(USAGE)?),
        _ => return Err(USAGE.into()),
    };
    if args.next().is_some() {
        return Err(USAGE.into());
    }
    let result =
        pf_detect::detect(&root).map_err(|error| format!("scan {}: {error}", root.display()))?;
    fs::create_dir_all(&output)?;
    fs::write(output.join("manifest.json"), result.manifest)?;
    fs::write(output.join("report.json"), result.report)?;
    if result.module_count == 0 {
        return Err("no supported PE modules found. See report.json for details.".into());
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("pf: {error}");
            ExitCode::FAILURE
        }
    }
}
