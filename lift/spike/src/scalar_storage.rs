use crate::address::Address;
use pf_decode::{
    DecodedOperandKind, Instruction, InstructionAttributes, MachineMode, Mnemonic, Register,
    RegisterClass,
};
use std::fmt::Write;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Operation {
    Move,
    Add,
    Adc,
    Sub,
    Sbb,
    And,
    Or,
    Xor,
    Compare,
    Test,
    Inc,
    Dec,
    Neg,
    Not,
    Cmov(u8),
}

impl Operation {
    fn writes(self) -> bool {
        !matches!(self, Self::Compare | Self::Test)
    }
    fn unary(self) -> bool {
        matches!(self, Self::Inc | Self::Dec | Self::Neg | Self::Not)
    }
    fn flags(self) -> Option<&'static str> {
        match self {
            Self::Add => Some("PF_ADD"),
            Self::Adc => Some("PF_ADC"),
            Self::Sub | Self::Compare | Self::Neg => Some("PF_SUB"),
            Self::Sbb => Some("PF_SBB"),
            Self::And | Self::Or | Self::Xor | Self::Test => Some("PF_LOGIC"),
            Self::Inc => Some("PF_INC"),
            Self::Dec => Some("PF_DEC"),
            _ => None,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
enum Location {
    Register { index: u8, shift: u8 },
    Memory(Address),
}
#[derive(Debug, PartialEq, Eq)]
enum Source {
    Location(Location),
    Immediate(u64),
    None,
}
#[derive(Debug, PartialEq, Eq)]
pub struct ScalarStorage {
    operation: Operation,
    width: u16,
    destination: Location,
    source: Source,
}

fn location(
    instruction: &Instruction,
    operand: &pf_decode::DecodedOperand,
) -> Result<Location, String> {
    let error = || "unsupported scalar storage location".to_owned();
    match &operand.kind {
        DecodedOperandKind::Reg(reg)
            if matches!(
                reg.class(),
                RegisterClass::GPR8
                    | RegisterClass::GPR16
                    | RegisterClass::GPR32
                    | RegisterClass::GPR64
            ) =>
        {
            Ok(Location::Register {
                index: reg.largest_enclosing(MachineMode::LONG_64).id(),
                shift: if matches!(
                    reg,
                    Register::AH | Register::CH | Register::DH | Register::BH
                ) {
                    8
                } else {
                    0
                },
            })
        }
        DecodedOperandKind::Mem(memory) => {
            Address::for_access(memory, instruction.address_width, instruction.length)
                .map(Location::Memory)
                .ok_or_else(error)
        }
        _ => Err(error()),
    }
}

pub fn decode(instruction: &Instruction) -> Option<Result<ScalarStorage, String>> {
    let op = match instruction.mnemonic {
        Mnemonic::MOV => Operation::Move,
        Mnemonic::ADD => Operation::Add,
        Mnemonic::ADC => Operation::Adc,
        Mnemonic::SUB => Operation::Sub,
        Mnemonic::SBB => Operation::Sbb,
        Mnemonic::AND => Operation::And,
        Mnemonic::OR => Operation::Or,
        Mnemonic::XOR => Operation::Xor,
        Mnemonic::CMP => Operation::Compare,
        Mnemonic::TEST => Operation::Test,
        Mnemonic::INC => Operation::Inc,
        Mnemonic::DEC => Operation::Dec,
        Mnemonic::NEG => Operation::Neg,
        Mnemonic::NOT => Operation::Not,
        Mnemonic::CMOVO
        | Mnemonic::CMOVNO
        | Mnemonic::CMOVB
        | Mnemonic::CMOVNB
        | Mnemonic::CMOVZ
        | Mnemonic::CMOVNZ
        | Mnemonic::CMOVBE
        | Mnemonic::CMOVNBE
        | Mnemonic::CMOVS
        | Mnemonic::CMOVNS
        | Mnemonic::CMOVP
        | Mnemonic::CMOVNP
        | Mnemonic::CMOVL
        | Mnemonic::CMOVNL
        | Mnemonic::CMOVLE
        | Mnemonic::CMOVNLE => Operation::Cmov(instruction.opcode & 15),
        _ => return None,
    };
    let operands = instruction.visible_operands();
    let destination = operands.first()?;
    let memory_destination = matches!(destination.kind, DecodedOperandKind::Mem(_));
    let memory_cmov = matches!(op, Operation::Cmov(_))
        && operands
            .get(1)
            .is_some_and(|s| matches!(s.kind, DecodedOperandKind::Mem(_)));
    if !matches!(destination.size, 8 | 16)
        && !(memory_destination && op.writes() && op != Operation::Move)
        && !memory_cmov
    {
        return None;
    }
    Some((|| {
        if instruction
            .attributes
            .contains(InstructionAttributes::HAS_LOCK)
        {
            return Err("atomic LOCK scalar storage is unsupported".into());
        }
        if !matches!(destination.size, 8 | 16 | 32 | 64)
            || operands.len() != if op.unary() { 1 } else { 2 }
        {
            return Err("unsupported scalar storage operands".into());
        }
        let destination_location = location(instruction, destination)?;
        let source = if op.unary() {
            Source::None
        } else {
            let operand = &operands[1];
            match &operand.kind {
                DecodedOperandKind::Imm(imm) if !imm.is_relative => Source::Immediate(imm.value),
                _ if operand.size == destination.size => {
                    Source::Location(location(instruction, operand)?)
                }
                _ => return Err("inconsistent scalar storage widths".into()),
            }
        };
        if matches!(op, Operation::Cmov(_)) && memory_destination {
            return Err("CMOV needs a register destination".into());
        }
        Ok(ScalarStorage {
            operation: op,
            width: destination.size,
            destination: destination_location,
            source,
        })
    })())
}

fn read(source: &mut String, location: &Location, name: &str, width: u16, write: bool) {
    match location {
        Location::Register { index, shift } => {
            writeln!(
                source,
                "      uint64_t {name} = (state->gpr[{index}] >> {shift}) & mask;"
            )
            .unwrap();
        }
        Location::Memory(address) => {
            writeln!(source, "      uint8_t *{name}_ptr;\n      if (!pf_guest_span(state, {}, {}, 1, {write}, &{name}_ptr)) return;", address.c_expression(), width / 8).unwrap();
            writeln!(source, "      uint64_t {name} = 0;\n      for (unsigned i = 0; i < {}; ++i) {name} |= (uint64_t){name}_ptr[i] << (8 * i);", width / 8).unwrap();
        }
    }
}

pub fn emit(instruction: &ScalarStorage, source: &mut String) {
    let width = instruction.width;
    let mask = if width == 64 {
        u64::MAX
    } else {
        (1_u64 << width) - 1
    };
    source.push_str("    {\n");
    writeln!(source, "      const uint64_t mask = UINT64_C({mask:#x});").unwrap();
    read(
        source,
        &instruction.destination,
        "lhs",
        width,
        instruction.operation.writes(),
    );
    match &instruction.source {
        Source::Location(location) => read(source, location, "rhs", width, false),
        Source::Immediate(bits) => {
            writeln!(source, "      uint64_t rhs = UINT64_C({bits:#x}) & mask;").unwrap();
        }
        Source::None => {
            source.push_str("      uint64_t rhs = 1;\n");
        }
    }
    source.push_str("      uint64_t old_flags = pf_read_flags(state);\n      (void)lhs; (void)rhs; (void)old_flags;\n");
    let expression = match instruction.operation {
        Operation::Move => "rhs".into(),
        Operation::Add | Operation::Inc => "lhs + rhs".into(),
        Operation::Adc => "lhs + rhs + (old_flags & 1)".into(),
        Operation::Sub | Operation::Compare | Operation::Dec => "lhs - rhs".into(),
        Operation::Sbb => "lhs - rhs - (old_flags & 1)".into(),
        Operation::And | Operation::Test => "lhs & rhs".into(),
        Operation::Or => "lhs | rhs".into(),
        Operation::Xor => "lhs ^ rhs".into(),
        Operation::Not => "lhs ^ mask".into(),
        Operation::Neg => {
            source.push_str("      rhs = lhs; lhs = 0;\n");
            "lhs - rhs".into()
        }
        Operation::Cmov(condition) => format!("pf_condition(old_flags, {condition}) ? rhs : lhs"),
    };
    writeln!(source, "      uint64_t result = ({expression}) & mask;").unwrap();
    if instruction.operation.writes() {
        match instruction.destination {
            Location::Register { index, shift } if width < 32 => {
                writeln!(source, "      state->gpr[{index}] = (state->gpr[{index}] & ~(mask << {shift})) | (result << {shift});").unwrap();
            }
            Location::Register { index, .. } => {
                writeln!(source, "      state->gpr[{index}] = result;").unwrap();
            }
            Location::Memory(_) => {
                writeln!(source, "      for (unsigned i = 0; i < {}; ++i) lhs_ptr[i] = (uint8_t)(result >> (8 * i));", width / 8).unwrap();
            }
        }
    }
    if let Some(mode) = instruction.operation.flags() {
        writeln!(source, "      state->rflags = old_flags;\n      state->pending = (struct pf_pending){{lhs, rhs, result, {width}, {mode}}};").unwrap();
    }
    source.push_str("    }\n");
}

#[cfg(test)]
mod tests {
    use super::*;
    use pf_decode::{Decoder, Mode};
    #[test]
    fn distinguishes_high_bytes_from_rex_low_bytes_and_extended_words() {
        for (bytes, destination, source) in [
            (&[0x88, 0xe0][..], (0, 0), (0, 8)),         // MOV AL, AH
            (&[0x40, 0x88, 0xe0], (0, 0), (4, 0)),       // MOV AL, SPL
            (&[0x40, 0x88, 0xcc], (4, 0), (1, 0)),       // MOV SPL, CL
            (&[0x45, 0x88, 0xc8], (8, 0), (9, 0)),       // MOV R8B, R9B
            (&[0x66, 0x45, 0x89, 0xc8], (8, 0), (9, 0)), // MOV R8W, R9W
        ] {
            let instruction = Decoder::new(Mode::X64).decode(bytes).unwrap();
            let decoded = decode(&instruction).unwrap().unwrap();
            assert_eq!(
                decoded.destination,
                Location::Register {
                    index: destination.0,
                    shift: destination.1
                }
            );
            assert_eq!(
                decoded.source,
                Source::Location(Location::Register {
                    index: source.0,
                    shift: source.1
                })
            );
        }
    }
    #[test]
    fn checks_atomic_and_segment_semantics_before_emission() {
        for bytes in [
            &[0xf0, 0x00, 0x0a][..],
            &[0x64, 0x00, 0x0a],
            &[0x65, 0x66, 0x0f, 0x44, 0x02],
        ] {
            let instruction = Decoder::new(Mode::X64).decode(bytes).unwrap();
            assert!(decode(&instruction).unwrap().is_err());
        }
    }
}
