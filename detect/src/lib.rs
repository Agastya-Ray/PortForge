pub mod pe;

use pe::{Pe, Symbol};
use std::collections::BTreeMap;
use std::fs;
use std::io::{self, ErrorKind, Read};
use std::path::Path;

fn quote(value: &str) -> String {
    let mut json = String::from("\"");
    for character in value.chars() {
        match character {
            '"' => json.push_str("\\\""),
            '\\' => json.push_str("\\\\"),
            '\u{0}'..='\u{1f}' => json.push_str(&format!("\\u{:04x}", character as u32)),
            _ => json.push(character),
        }
    }
    json.push('"');
    json
}

fn array(items: impl IntoIterator<Item = String>) -> String {
    format!("[{}]", items.into_iter().collect::<Vec<_>>().join(","))
}

fn object<'a>(fields: impl IntoIterator<Item = (&'a str, String)>) -> String {
    let fields = fields
        .into_iter()
        .map(|(key, value)| format!("{}:{value}", quote(key)));
    format!("{{{}}}", fields.collect::<Vec<_>>().join(","))
}

fn inventory(root: &Path, prefix: &str, paths: &mut Vec<String>) -> io::Result<()> {
    for entry in fs::read_dir(root)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        if !kind.is_file() && !kind.is_dir() {
            continue;
        }
        let name = entry.file_name();
        let name = name.to_str().ok_or_else(|| {
            io::Error::new(
                ErrorKind::InvalidData,
                format!("non-UTF-8 filename under {}", root.display()),
            )
        })?;
        let relative = format!("{prefix}{name}");
        if kind.is_dir() {
            inventory(&entry.path(), &format!("{relative}/"), paths)?;
        } else {
            paths.push(relative);
        }
    }
    Ok(())
}

fn engine(root: &Path, paths: &[String]) -> io::Result<Vec<String>> {
    let mut matches = Vec::new();
    for path in paths {
        let lower = path.to_ascii_lowercase();
        let rule = if lower.ends_with("_data/globalgamemanagers") {
            "unity"
        } else if lower == "renpy/__init__.py" {
            "renpy"
        } else if lower == "js/rpg_core.js" || lower.ends_with("/js/rpg_core.js") {
            "rpg_maker_mv"
        } else if lower == "js/rmmz_core.js" || lower.ends_with("/js/rmmz_core.js") {
            "rpg_maker_mz"
        } else if lower.ends_with(".pck") {
            let mut magic = [0; 4];
            match fs::File::open(root.join(path))?.read_exact(&mut magic) {
                Ok(()) if &magic == b"GDPC" => {}
                Ok(()) => continue,
                Err(error) if error.kind() == ErrorKind::UnexpectedEof => continue,
                Err(error) => return Err(error),
            }
            "godot"
        } else {
            continue;
        };
        matches.push(object([("id", quote(rule)), ("path", quote(path))]));
    }
    Ok(matches)
}

pub struct Detection {
    pub manifest: String,
    pub report: String,
    pub module_count: usize,
}

pub fn detect(root: &Path) -> io::Result<Detection> {
    let mut paths = Vec::new();
    inventory(root, "", &mut paths)?;
    paths.sort_unstable();
    let engines = engine(root, &paths)?;
    let mut modules = Vec::new();
    let mut failures = Vec::new();
    let mut graphics = BTreeMap::new();
    let mut input = BTreeMap::new();

    for path in &paths {
        let lower = path.to_ascii_lowercase();
        if !lower.ends_with(".exe") && !lower.ends_with(".dll") {
            continue;
        }
        let data = fs::read(root.join(path))?;
        let module = Pe::parse(&data).and_then(|pe| pe.imports().map(|imports| (pe, imports)));
        let (pe, imports) = match module {
            Ok(module) => module,
            Err(error) => {
                failures.push(object([
                    ("path", quote(path)),
                    ("reason", quote(&error.to_string())),
                ]));
                continue;
            }
        };
        for import in &imports {
            let evidence = object([("path", quote(path)), ("dll", quote(&import.dll))]);
            let graphics_api = match import.dll.as_str() {
                "ddraw.dll" => "directdraw",
                "d3d8.dll" => "d3d8",
                "d3d9.dll" => "d3d9",
                "d3d10.dll" | "d3d10_1.dll" => "d3d10",
                "d3d11.dll" => "d3d11",
                "d3d12.dll" => "d3d12",
                "opengl32.dll" => "opengl",
                "vulkan-1.dll" => "vulkan",
                _ => "",
            };
            if !graphics_api.is_empty() {
                graphics
                    .entry(graphics_api)
                    .or_insert_with(Vec::new)
                    .push(evidence.clone());
            }
            let input_api = match import.dll.as_str() {
                "xinput1_1.dll" | "xinput1_2.dll" | "xinput1_3.dll" | "xinput1_4.dll"
                | "xinput9_1_0.dll" => "xinput",
                "dinput.dll" | "dinput8.dll" => "directinput",
                "user32.dll"
                    if import.symbols.iter().any(|symbol| {
                        matches!(symbol,
                    Symbol::Name(name) if matches!(name.as_str(),
                        "RegisterRawInputDevices" | "GetRawInputData"))
                    }) =>
                {
                    "raw_input"
                }
                _ => "",
            };
            if !input_api.is_empty() {
                input
                    .entry(input_api)
                    .or_insert_with(Vec::new)
                    .push(evidence);
            }
        }
        modules.push(object([
            ("path", quote(path)),
            ("kind", quote(if pe.is_dll { "dll" } else { "exe" })),
            ("architecture", quote(pe.arch.name())),
            ("entry_rva", pe.entry_rva.to_string()),
            ("image_base", pe.image_base.to_string()),
            ("image_size", pe.image_size.to_string()),
            (
                "imports",
                array(imports.into_iter().map(|import| {
                    object([
                        ("dll", quote(&import.dll)),
                        ("delayed", import.delayed.to_string()),
                        (
                            "symbols",
                            array(import.symbols.into_iter().map(|symbol| match symbol {
                                Symbol::Name(name) => object([("name", quote(&name))]),
                                Symbol::Ordinal(ordinal) => {
                                    object([("ordinal", ordinal.to_string())])
                                }
                            })),
                        ),
                    ])
                })),
            ),
        ]));
    }
    let evidence_json = |evidence: BTreeMap<&str, Vec<String>>| {
        array(
            evidence
                .into_iter()
                .map(|(api, paths)| object([("api", quote(api)), ("evidence", array(paths))])),
        )
    };
    let module_count = modules.len();
    let engine = array(engines);
    let graphics = evidence_json(graphics);
    let input = evidence_json(input);
    let manifest = object([
        ("schema_version", "1".into()),
        ("modules", array(modules)),
        ("engine", engine.clone()),
        ("graphics", graphics.clone()),
        ("input", input.clone()),
    ]);
    let report = object([
        ("schema_version", "1".into()),
        ("stage", quote("detect")),
        ("modules_parsed", module_count.to_string()),
        ("failures", array(failures)),
        ("engine_evidence", engine),
        ("graphics_evidence", graphics),
        ("input_evidence", input),
    ]);
    Ok(Detection {
        manifest: format!("{manifest}\n"),
        report: format!("{report}\n"),
        module_count,
    })
}
