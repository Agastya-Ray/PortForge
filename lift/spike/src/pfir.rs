use pf_decode::{DecodedOperandKind, Decoder, Mnemonic, Mode, RegisterClass};
use std::fmt::Write;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Width {
    U32,
    U64,
}

impl Width {
    fn bits(self) -> u8 {
        match self {
            Self::U32 => 32,
            Self::U64 => 64,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arithmetic {
    Add,
    Subtract,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Value(usize);

#[derive(Debug, PartialEq, Eq)]
pub enum Effect {
    Read {
        into: Value,
        register: u8,
        width: Width,
    },
    Binary {
        into: Value,
        operation: Arithmetic,
        width: Width,
        lhs: Value,
        rhs: Value,
    },
    Write {
        register: u8,
        from: Value,
    },
    PendingFlags {
        operation: Arithmetic,
        width: Width,
        lhs: Value,
        rhs: Value,
        result: Value,
    },
    AdvanceRip(u8),
}

pub fn lift(bytes: &[u8]) -> Result<Vec<Effect>, String> {
    let decoder = Decoder::new(Mode::X64);
    let mut effects = Vec::new();
    let mut offset = 0;
    let mut next_value = 0;
    while offset < bytes.len() {
        let instruction = decoder
            .decode(&bytes[offset..])
            .map_err(|error| format!("{error} at offset {offset}"))?;
        let (operation, writes) = match instruction.mnemonic {
            Mnemonic::ADD => (Arithmetic::Add, true),
            Mnemonic::SUB => (Arithmetic::Subtract, true),
            Mnemonic::CMP => (Arithmetic::Subtract, false),
            mnemonic => {
                return Err(format!(
                    "decoded {mnemonic} at offset {offset}; no lift available"
                ))
            }
        };
        let unsupported = || {
            format!(
                "decoded {} at offset {offset}; unsupported operands",
                instruction.mnemonic
            )
        };
        let [destination, source] = instruction.visible_operands() else {
            return Err(unsupported());
        };
        let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Reg(source)) =
            (&destination.kind, &source.kind)
        else {
            return Err(unsupported());
        };
        let width = match (destination.class(), source.class()) {
            (RegisterClass::GPR32, RegisterClass::GPR32) => Width::U32,
            (RegisterClass::GPR64, RegisterClass::GPR64) => Width::U64,
            _ => return Err(unsupported()),
        };
        let (destination, source) = (destination.id(), source.id());
        let lhs = Value(next_value);
        let rhs = Value(next_value + 1);
        let result = Value(next_value + 2);
        let length = instruction.length;
        effects.extend([
            Effect::Read {
                into: lhs,
                register: destination,
                width,
            },
            Effect::Read {
                into: rhs,
                register: source,
                width,
            },
            Effect::Binary {
                into: result,
                operation,
                width,
                lhs,
                rhs,
            },
            Effect::PendingFlags {
                operation,
                width,
                lhs,
                rhs,
                result,
            },
        ]);
        if writes {
            effects.push(Effect::Write {
                register: destination,
                from: result,
            });
        }
        effects.push(Effect::AdvanceRip(length));
        next_value += 3;
        offset += usize::from(length);
    }
    Ok(effects)
}

pub fn emit_c(block: &[Effect], source: &mut String) {
    for effect in block {
        match *effect {
            Effect::Read {
                into: Value(id),
                register,
                width,
            } => {
                let width = width.bits();
                writeln!(
                    source,
                    "    uint{width}_t v{id} = (uint{width}_t)state->gpr[{register}];"
                )
                .unwrap();
            }
            Effect::Binary {
                into: Value(id),
                operation,
                width,
                lhs: Value(lhs),
                rhs: Value(rhs),
            } => {
                let operator = if operation == Arithmetic::Add {
                    '+'
                } else {
                    '-'
                };
                let width = width.bits();
                writeln!(
                    source,
                    "    uint{width}_t v{id} = v{lhs} {operator} v{rhs};"
                )
                .unwrap();
            }
            Effect::Write {
                register,
                from: Value(id),
            } => {
                writeln!(source, "    state->gpr[{register}] = v{id};").unwrap();
            }
            Effect::PendingFlags {
                operation,
                width,
                lhs: Value(lhs),
                rhs: Value(rhs),
                result: Value(id),
            } => {
                let mode = if operation == Arithmetic::Add {
                    "PF_ADD"
                } else {
                    "PF_SUB"
                };
                let width = width.bits();
                writeln!(source, "    state->pending = (struct pf_pending){{v{lhs}, v{rhs}, v{id}, {width}, {mode}}};").unwrap();
            }
            Effect::AdvanceRip(bytes) => {
                writeln!(source, "    state->rip += {bytes};").unwrap();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_extended_registers_and_both_operand_orders() {
        let forward = lift(&[0x4d, 0x01, 0xc8]).unwrap();
        let reverse = lift(&[0x4d, 0x03, 0xc1]).unwrap();
        assert_eq!(forward, reverse);
        assert!(matches!(
            forward[0],
            Effect::Read {
                register: 8,
                width: Width::U64,
                ..
            }
        ));
        assert!(matches!(
            forward[1],
            Effect::Read {
                register: 9,
                width: Width::U64,
                ..
            }
        ));
    }

    #[test]
    fn decodes_independent_rex_bits_and_register_aliases() {
        for (bytes, destination, source) in [
            ([0x44, 0x01, 0xc8], 0, 9),
            ([0x41, 0x01, 0xc8], 8, 1),
            ([0x45, 0x29, 0xc0], 8, 8),
        ] {
            let effects = lift(&bytes).unwrap();
            assert!(matches!(effects[0], Effect::Read { register, .. } if register == destination));
            assert!(matches!(effects[1], Effect::Read { register, .. } if register == source));
        }
    }

    #[test]
    fn rejects_truncated_and_unsupported_encodings() {
        for bytes in [
            &[0x48][..],
            &[0x01],
            &[0x01, 0x08],
            &[0x90],
            &[0xf0, 0x01, 0xc8],
        ] {
            assert!(lift(bytes).is_err(), "{bytes:02x?}");
        }
    }
    #[test]
    fn distinguishes_decode_failure_from_missing_semantics() {
        assert!(lift(&[0x48, 0x01]).unwrap_err().contains("truncated"));
        assert!(lift(&[0xd9, 0xe8]).unwrap_err().contains("decoded fld1"));
        assert!(lift(&[0x01, 0x08])
            .unwrap_err()
            .contains("unsupported operands"));
    }
}
