use crate::pfir::{self, Effect};
use pf_decode::{DecodedOperandKind, Instruction, Mnemonic, PeDecoder, Register, RegisterClass};
use pf_detect::pe::{Architecture, Import, Pe, Symbol};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

const MAX_INSTRUCTIONS: usize = 4096;

type Imports<'a> = BTreeMap<u32, (&'a str, &'a Symbol)>;

fn import_exits(dll: &str, symbol: &Symbol) -> bool {
    dll.eq_ignore_ascii_case("kernel32.dll")
        && matches!(symbol, Symbol::Name(name) if name == "ExitProcess")
}

fn import_slots<'a>(image: &Pe<'_>, imports: &'a [Import]) -> Result<Imports<'a>, String> {
    let mut slots = BTreeMap::new();
    for import in imports {
        let Some(first) = image
            .eager_iat(import)
            .map_err(|error| format!("{} eager imports: {error}", import.dll))?
        else {
            continue;
        };
        let mut slot = first;
        for symbol in &import.symbols {
            let next = slot + 8;
            if slots.range(slot.saturating_sub(7)..next).next().is_some() {
                return Err(format!("overlapping IAT slot RVA {slot:#x}"));
            }
            slots.insert(slot, (import.dll.as_str(), symbol));
            slot = next;
        }
    }
    Ok(slots)
}

enum Step {
    Lifted(Vec<Effect>),
    Jump(u32),
    Conditional { target: u32, condition: u8 },
    Call(u32),
    RegisterCall(u8),
    ImportCall { slot: u32, returns: bool },
    Return,
}

impl Step {
    fn decode(instruction: Instruction, rva: u32, imports: &Imports<'_>) -> Result<Self, String> {
        let mnemonic = instruction.mnemonic;
        match mnemonic {
            Mnemonic::RET if instruction.opcode == 0xc3 && instruction.length == 1 => {
                return Ok(Self::Return);
            }
            Mnemonic::RET => return Err("only unprefixed near RET is supported".into()),
            Mnemonic::JMP
            | Mnemonic::CALL
            | Mnemonic::JO
            | Mnemonic::JNO
            | Mnemonic::JB
            | Mnemonic::JNB
            | Mnemonic::JZ
            | Mnemonic::JNZ
            | Mnemonic::JBE
            | Mnemonic::JNBE
            | Mnemonic::JS
            | Mnemonic::JNS
            | Mnemonic::JP
            | Mnemonic::JNP
            | Mnemonic::JL
            | Mnemonic::JNL
            | Mnemonic::JLE
            | Mnemonic::JNLE => {}
            _ => return pfir::lift_instruction(instruction).map(Self::Lifted),
        }
        let [operand] = instruction.visible_operands() else {
            return Err(format!("unsupported {mnemonic:?} operands"));
        };
        if let DecodedOperandKind::Reg(register) = operand.kind {
            if mnemonic == Mnemonic::CALL && register.class() == RegisterClass::GPR64 {
                return Ok(Self::RegisterCall(register.id()));
            }
        }
        if let DecodedOperandKind::Mem(memory) = &operand.kind {
            if mnemonic == Mnemonic::CALL
                && operand.size == 64
                && memory.base == Register::RIP
                && memory.index == Register::NONE
                && !matches!(memory.segment, Register::FS | Register::GS)
            {
                let slot = instruction
                    .calc_absolute_address(u64::from(rva), operand)
                    .ok()
                    .and_then(|address| u32::try_from(address).ok())
                    .ok_or("RIP-relative CALL slot falls outside the image RVA range")?;
                let (dll, symbol) = imports.get(&slot).ok_or_else(|| {
                    format!("RIP-relative CALL slot RVA {slot:#x} is not an eager import")
                })?;
                return Ok(Self::ImportCall {
                    slot,
                    returns: !import_exits(dll, symbol),
                });
            }
        }
        if !matches!(&operand.kind, DecodedOperandKind::Imm(immediate) if immediate.is_relative) {
            return Err(format!("unsupported nonrelative {mnemonic:?} target"));
        }
        let target = instruction
            .calc_absolute_address(u64::from(rva), operand)
            .ok()
            .and_then(|address| u32::try_from(address).ok())
            .ok_or_else(|| format!("{mnemonic:?} target falls outside the image RVA range"))?;
        Ok(match mnemonic {
            Mnemonic::JMP => Self::Jump(target),
            Mnemonic::CALL => Self::Call(target),
            _ => Self::Conditional {
                target,
                condition: instruction.opcode & 15,
            },
        })
    }

    fn add_successors(&self, next: u32, pending: &mut BTreeSet<u32>) {
        match *self {
            Self::Lifted(_) | Self::RegisterCall(_) | Self::ImportCall { returns: true, .. } => {
                pending.insert(next);
            }
            Self::Jump(target) => {
                pending.insert(target);
            }
            Self::Conditional { target, .. } | Self::Call(target) => {
                pending.extend([next, target]);
            }
            Self::Return | Self::ImportCall { returns: false, .. } => {}
        }
    }

    fn emit(&self, next: u32, base: u64, source: &mut String) {
        let address = |rva| format!("UINT64_C({:#x})", base + u64::from(rva));
        match *self {
            Self::Lifted(ref effects) => pfir::emit_c(effects, source),
            Self::Jump(target) => {
                writeln!(source, "    state->rip = {};", address(target)).unwrap()
            }
            Self::Conditional { target, condition } => {
                writeln!(
                    source,
                    "    state->rip = pf_condition(pf_read_flags(state), {condition}) ? {} : {};",
                    address(target),
                    address(next)
                )
                .unwrap();
            }
            Self::Call(target) => {
                writeln!(
                    source,
                    "    pf_pe_call(state, {}, {});",
                    address(target),
                    address(next)
                )
                .unwrap();
            }
            Self::Return => writeln!(source, "    pf_pe_return(state);").unwrap(),
            Self::ImportCall { .. } | Self::RegisterCall(_) => {
                unreachable!("import calls need host bindings")
            }
        }
    }
}

fn discover(image: &Pe<'_>, imports: &Imports<'_>) -> Result<BTreeMap<u32, (u32, Step)>, String> {
    let decoder = PeDecoder::new(image);
    let mut pending = BTreeSet::from([image.entry_rva]);
    let mut instructions = BTreeMap::new();
    while let Some(rva) = pending.pop_first() {
        if instructions.contains_key(&rva) {
            continue;
        }
        if instructions.len() == MAX_INSTRUCTIONS {
            return Err(format!(
                "instruction discovery limit exceeded at RVA {rva:#x}"
            ));
        }
        if instructions
            .range(..rva)
            .next_back()
            .is_some_and(|(_, (end, _))| *end > rva)
        {
            return Err(format!(
                "instruction target overlaps a decoded instruction at RVA {rva:#x}"
            ));
        }
        let instruction = decoder.decode_at(rva).map_err(|error| error.to_string())?;
        let next = rva + u32::from(instruction.length);
        if let Some((target, _)) = instructions.range(rva..next).next() {
            return Err(format!(
                "instruction at RVA {rva:#x} overlaps a decoded target at RVA {target:#x}"
            ));
        }
        let step = Step::decode(instruction, rva, imports)
            .map_err(|error| format!("RVA {rva:#x}: {error}"))?;
        step.add_successors(next, &mut pending);
        instructions.insert(rva, (next, step));
    }
    Ok(instructions)
}

fn c_string(value: &str) -> String {
    let mut literal = String::from("\"");
    for byte in value.bytes() {
        write!(literal, "\\{byte:03o}").unwrap();
    }
    literal.push('"');
    literal
}

fn executable(bytes: &[u8]) -> Result<Pe<'_>, String> {
    let image = Pe::parse(bytes).map_err(|error| error.to_string())?;
    if !matches!(image.arch, Architecture::X64) || image.is_dll || image.entry_rva == 0 {
        return Err("expected a PE32+ executable with an entry point".into());
    }
    let base = image.image_base;
    let last = base.checked_add(u64::from(image.image_size) - 1);
    if !last.is_some_and(|last| last <= 0x0000_7fff_ffff_ffff || base >= 0xffff_8000_0000_0000) {
        return Err("preferred PE image range is not canonical".into());
    }
    Ok(image)
}

pub fn map(bytes: &[u8]) -> Result<Vec<u8>, String> {
    const IMAGE_BUDGET: usize = 64 * 1024 * 1024;
    let image = executable(bytes)?;
    image
        .map_image(image.image_base, IMAGE_BUDGET)
        .map_err(|error| error.to_string())
}

pub fn emit(bytes: &[u8], source: &mut String) -> Result<(), String> {
    let image = executable(bytes)?;
    let base = image.image_base;
    let imported = image
        .imports()
        .map_err(|error| format!("imports: {error}"))?;
    let imports = import_slots(&image, &imported)?;
    let instructions = discover(&image, &imports)?;
    let used_slots: BTreeSet<_> = if instructions
        .values()
        .any(|(_, step)| matches!(step, Step::RegisterCall(_)))
    {
        imports.keys().copied().collect()
    } else {
        instructions
            .values()
            .filter_map(|(_, step)| match step {
                Step::ImportCall { slot, .. } => Some(*slot),
                _ => None,
            })
            .collect()
    };
    let indices: BTreeMap<_, _> = used_slots
        .into_iter()
        .enumerate()
        .map(|(index, slot)| (slot, index))
        .collect();
    source.push_str(include_str!("program.h"));
    writeln!(source, "#define PF_PE_IMAGE_BASE UINT64_C({base:#x})").unwrap();
    writeln!(source, "#define PF_PE_IMAGE_SIZE {}u", image.image_size).unwrap();
    writeln!(
        source,
        "#define PF_PE_ENTRY UINT64_C({:#x})",
        base + u64::from(image.entry_rva)
    )
    .unwrap();
    writeln!(
        source,
        "const size_t pf_pe_import_count = {};",
        indices.len()
    )
    .unwrap();
    source.push_str("const struct pf_pe_import pf_pe_imports[] = {\n");
    if indices.is_empty() {
        source.push_str("    {0},\n");
    }
    for slot in indices.keys() {
        let (dll, symbol) = imports[slot];
        let exits = if import_exits(dll, symbol) {
            "true"
        } else {
            "false"
        };
        let (name, ordinal) = match symbol {
            Symbol::Name(name) => (c_string(name), 0),
            Symbol::Ordinal(ordinal) => ("NULL".into(), *ordinal),
        };
        writeln!(
            source,
            "    {{UINT64_C({:#x}), {}, {name}, {ordinal}, {exits}}},",
            base + u64::from(*slot),
            c_string(dll)
        )
        .unwrap();
    }
    source.push_str("};\n");
    for (rva, (next, step)) in &instructions {
        if matches!(step, Step::ImportCall { .. } | Step::RegisterCall(_)) {
            continue;
        }
        writeln!(
            source,
            "static void pf_pe_{rva:x}(struct pf_state *state) {{"
        )
        .unwrap();
        step.emit(*next, base, source);
        writeln!(source, "}}").unwrap();
    }
    source.push_str(
        r#"
enum pf_pe_status pf_pe_run_bound(struct pf_state *state, uint64_t stop_rip, size_t fuel,
                                  const struct pf_pe_binding *bindings, size_t binding_count) {
    (void)bindings;
    (void)binding_count;
    for (;;) {
        if (state->memory_fault != PF_MEM_OK) return PF_PE_MEMORY_FAULT;
        if (state->x87_unsupported) return PF_PE_X87_UNSUPPORTED;
        if (state->pe_exited) return PF_PE_EXITED;
        if (state->rip == stop_rip) return PF_PE_RETURNED;
        if (!fuel) return PF_PE_BUDGET;
        --fuel;
        switch (state->rip) {
"#,
    );
    for (rva, (next, step)) in &instructions {
        let pc = base + u64::from(*rva);
        let return_rip = base + u64::from(*next);
        match step {
            Step::ImportCall { slot, .. } => {
                let index = indices[slot];
                writeln!(source, "        case UINT64_C({pc:#x}): {{").unwrap();
                writeln!(source, "            const struct pf_pe_binding *binding = bindings && binding_count > {index} ? &bindings[{index}] : NULL;").unwrap();
                writeln!(source, "            enum pf_pe_status status = pf_pe_import_call(state, pf_pe_imports[{index}].slot, binding, UINT64_C({return_rip:#x}));").unwrap();
            }
            Step::RegisterCall(register) => {
                writeln!(source, "        case UINT64_C({pc:#x}): {{").unwrap();
                writeln!(source, "            enum pf_pe_status status = pf_pe_register_call(state, state->gpr[{register}], bindings, binding_count, UINT64_C({return_rip:#x}));").unwrap();
            }
            _ => {
                writeln!(
                    source,
                    "        case UINT64_C({pc:#x}): pf_pe_{rva:x}(state); break;"
                )
                .unwrap();
                continue;
            }
        }
        source.push_str("            if (status != PF_PE_CONTINUE) return status;\n            break;\n        }\n");
    }
    source.push_str("        default: return PF_PE_UNKNOWN_RIP;\n        }\n    }\n}\n");
    source.push_str(
        r#"
enum pf_pe_status pf_pe_run(struct pf_state *state, uint64_t stop_rip, size_t fuel) {
    return pf_pe_run_bound(state, stop_rip, fuel, NULL, 0);
}
"#,
    );
    Ok(())
}
