use crate::pfir::{self, Effect};
use pf_decode::{DecodedOperandKind, Instruction, Mnemonic, PeDecoder};
use pf_detect::pe::{Architecture, Pe};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

const MAX_INSTRUCTIONS: usize = 4096;

enum Step {
    Lifted(Vec<Effect>),
    Jump(u32),
    Conditional { target: u32, zero: bool },
    Call(u32),
    Return,
}

impl Step {
    fn decode(instruction: Instruction, rva: u32) -> Result<Self, String> {
        let mnemonic = instruction.mnemonic;
        match mnemonic {
            Mnemonic::RET if instruction.opcode == 0xc3 && instruction.length == 1 => {
                return Ok(Self::Return);
            }
            Mnemonic::RET => return Err("only unprefixed near RET is supported".into()),
            Mnemonic::JMP | Mnemonic::JZ | Mnemonic::JNZ | Mnemonic::CALL => {}
            _ => return pfir::lift_instruction(instruction).map(Self::Lifted),
        }
        let [operand] = instruction.visible_operands() else {
            return Err(format!("unsupported {mnemonic:?} operands"));
        };
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
                zero: mnemonic == Mnemonic::JZ,
            },
        })
    }

    fn add_successors(&self, next: u32, pending: &mut BTreeSet<u32>) {
        match *self {
            Self::Lifted(_) => {
                pending.insert(next);
            }
            Self::Jump(target) => {
                pending.insert(target);
            }
            Self::Conditional { target, .. } | Self::Call(target) => {
                pending.extend([next, target]);
            }
            Self::Return => {}
        }
    }

    fn emit(&self, next: u32, base: u64, source: &mut String) {
        let address = |rva| format!("UINT64_C({:#x})", base + u64::from(rva));
        match *self {
            Self::Lifted(ref effects) => pfir::emit_c(effects, source),
            Self::Jump(target) => {
                writeln!(source, "    state->rip = {};", address(target)).unwrap()
            }
            Self::Conditional { target, zero } => {
                let compare = if zero { "!=" } else { "==" };
                writeln!(
                    source,
                    "    state->rip = (pf_read_flags(state) & UINT64_C(0x40)) {compare} 0 ? {} : {};",
                    address(target), address(next)
                ).unwrap();
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
        }
    }
}

fn discover(image: &Pe<'_>) -> Result<BTreeMap<u32, (u32, Step)>, String> {
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
        let step =
            Step::decode(instruction, rva).map_err(|error| format!("RVA {rva:#x}: {error}"))?;
        step.add_successors(next, &mut pending);
        instructions.insert(rva, (next, step));
    }
    Ok(instructions)
}

pub fn emit(bytes: &[u8], source: &mut String) -> Result<(), String> {
    let image = Pe::parse(bytes).map_err(|error| error.to_string())?;
    if !matches!(image.arch, Architecture::X64) || image.is_dll || image.entry_rva == 0 {
        return Err("expected a PE32+ executable with an entry point".into());
    }
    let base = image.image_base;
    let last = base.checked_add(u64::from(image.image_size) - 1);
    if !last.is_some_and(|last| last <= 0x0000_7fff_ffff_ffff || base >= 0xffff_8000_0000_0000) {
        return Err("preferred PE image range is not canonical".into());
    }
    let instructions = discover(&image)?;
    source.push_str(include_str!("program.h"));
    writeln!(
        source,
        "#define PF_PE_ENTRY UINT64_C({:#x})",
        base + u64::from(image.entry_rva)
    )
    .unwrap();
    for (rva, (next, step)) in &instructions {
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
enum pf_pe_status pf_pe_run(struct pf_state *state, uint64_t stop_rip, size_t fuel) {
    for (;;) {
        if (state->memory_fault != PF_MEM_OK) return PF_PE_MEMORY_FAULT;
        if (state->x87_unsupported) return PF_PE_X87_UNSUPPORTED;
        if (state->rip == stop_rip) return PF_PE_RETURNED;
        if (!fuel) return PF_PE_BUDGET;
        --fuel;
        switch (state->rip) {
"#,
    );
    for rva in instructions.keys() {
        writeln!(
            source,
            "        case UINT64_C({:#x}): pf_pe_{rva:x}(state); break;",
            base + u64::from(*rva)
        )
        .unwrap();
    }
    source.push_str("        default: return PF_PE_UNKNOWN_RIP;\n        }\n    }\n}\n");
    Ok(())
}
