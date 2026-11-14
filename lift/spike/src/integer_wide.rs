use crate::address::Address;
use pf_decode::{DecodedOperandKind, Instruction, MachineMode, Mnemonic, Register, RegisterClass};
use std::fmt::Write;

#[derive(Debug, PartialEq, Eq)]
pub enum WideInteger {
    Operation {
        name: &'static str,
        width: u16,
        source: WideSource,
    },
    Extend {
        from: u8,
        to: u8,
        high: bool,
    },
}
#[derive(Debug, PartialEq, Eq)]
pub enum WideSource {
    Register { index: u8, shift: u8 },
    Memory(Address),
}

pub fn decode(instruction: &Instruction) -> Option<Result<WideInteger, String>> {
    let extension = match instruction.mnemonic {
        Mnemonic::CBW => Some((8, 16, false)),
        Mnemonic::CWDE => Some((16, 32, false)),
        Mnemonic::CDQE => Some((32, 64, false)),
        Mnemonic::CWD => Some((16, 16, true)),
        Mnemonic::CDQ => Some((32, 32, true)),
        Mnemonic::CQO => Some((64, 64, true)),
        _ => None,
    };
    if let Some((from, to, high)) = extension {
        return Some(Ok(WideInteger::Extend { from, to, high }));
    }
    let name = match instruction.mnemonic {
        Mnemonic::MUL => "PF_FULL_MUL",
        Mnemonic::IMUL if instruction.visible_operands().len() == 1 => "PF_FULL_IMUL",
        Mnemonic::DIV => "PF_FULL_DIV",
        Mnemonic::IDIV => "PF_FULL_IDIV",
        _ => return None,
    };
    Some((|| {
        let [operand] = instruction.visible_operands() else {
            return Err("full integer operation needs one explicit operand".into());
        };
        let width = operand.size;
        if !matches!(width, 8 | 16 | 32 | 64) {
            return Err("unsupported full integer width".into());
        }
        let source = match &operand.kind {
            DecodedOperandKind::Reg(reg)
                if matches!(
                    reg.class(),
                    RegisterClass::GPR8
                        | RegisterClass::GPR16
                        | RegisterClass::GPR32
                        | RegisterClass::GPR64
                ) =>
            {
                WideSource::Register {
                    index: reg.largest_enclosing(MachineMode::LONG_64).id(),
                    shift: if matches!(
                        reg,
                        Register::AH | Register::CH | Register::DH | Register::BH
                    ) {
                        8
                    } else {
                        0
                    },
                }
            }
            DecodedOperandKind::Mem(memory) => WideSource::Memory(
                Address::for_access(memory, instruction.address_width, instruction.length)
                    .ok_or("segmented full integer operand is unsupported")?,
            ),
            _ => return Err("unsupported full integer operand".into()),
        };
        Ok(WideInteger::Operation {
            name,
            width,
            source,
        })
    })())
}

pub fn emit(instruction: &WideInteger, output: &mut String) {
    match instruction {
        WideInteger::Extend { from, to, high } => {
            writeln!(
                output,
                "    pf_integer_extend(state, {from}, {to}, {high});"
            )
            .unwrap();
        }
        WideInteger::Operation {
            name,
            width,
            source,
        } => {
            output.push_str("    {\n");
            match source {
                WideSource::Register { index, shift } => {
                    writeln!(
                        output,
                        "      uint64_t operand = state->gpr[{index}] >> {shift};"
                    )
                    .unwrap();
                }
                WideSource::Memory(address) => {
                    writeln!(output, "      uint8_t *bytes;\n      if (!pf_guest_span(state, {}, {}, 1, false, &bytes)) return;", address.c_expression(), width / 8).unwrap();
                    writeln!(output, "      uint64_t operand = 0;\n      for (unsigned i = 0; i < {}; ++i) operand |= (uint64_t)bytes[i] << (8 * i);", width / 8).unwrap();
                }
            }
            writeln!(
                output,
                "      if (!pf_integer_full(state, {name}, {width}, operand)) return;\n    }}"
            )
            .unwrap();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use pf_decode::{Decoder, Mode};

    #[test]
    fn rejects_segmented_full_width_memory_and_maps_byte_aliases() {
        for bytes in [&[0x64, 0x48, 0xf7, 0x37][..], &[0x65, 0xf6, 0x27]] {
            let decoded = Decoder::new(Mode::X64).decode(bytes).unwrap();
            assert!(decode(&decoded).unwrap().is_err());
        }
        for (bytes, index, shift) in [
            (&[0xf6, 0xe4][..], 0, 8),   // MUL AH
            (&[0x40, 0xf6, 0xe4], 4, 0), // MUL SPL
            (&[0x41, 0xf6, 0xe0], 8, 0), // MUL R8B
        ] {
            let decoded = Decoder::new(Mode::X64).decode(bytes).unwrap();
            assert!(
                matches!(decode(&decoded).unwrap().unwrap(), WideInteger::Operation {
                source: WideSource::Register { index: actual, shift: offset }, ..
            } if actual == index && offset == shift)
            );
        }
    }
}
