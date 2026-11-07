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
    let mut effects = Vec::new();
    let mut offset = 0;
    let mut next_value = 0;
    while offset < bytes.len() {
        let instruction = &bytes[offset..];
        let (rex, opcode_offset) = match instruction[0] {
            prefix @ 0x40..=0x4f => (prefix, 1),
            _ => (0, 0),
        };
        let [opcode, modrm] = *instruction
            .get(opcode_offset..opcode_offset + 2)
            .ok_or_else(|| format!("truncated instruction at offset {offset}"))?
        else {
            unreachable!("two-byte slice");
        };
        let (operation, writes) = match opcode {
            0x01 | 0x03 => (Arithmetic::Add, true),
            0x29 | 0x2b => (Arithmetic::Subtract, true),
            0x39 | 0x3b => (Arithmetic::Subtract, false),
            _ => {
                return Err(format!(
                    "unsupported opcode {opcode:#04x} at offset {offset}"
                ))
            }
        };
        if modrm & 0xc0 != 0xc0 {
            return Err(format!("unsupported memory operand at offset {offset}"));
        }
        let reg = ((modrm >> 3) & 7) | ((rex & 4) << 1);
        let rm = (modrm & 7) | ((rex & 1) << 3);
        let (destination, source) = if opcode & 2 == 0 {
            (rm, reg)
        } else {
            (reg, rm)
        };
        let width = if rex & 8 == 0 { Width::U32 } else { Width::U64 };
        let lhs = Value(next_value);
        let rhs = Value(next_value + 1);
        let result = Value(next_value + 2);
        let length = (opcode_offset + 2) as u8;
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
            &[0x48, 0x48, 0x01, 0xc8],
        ] {
            assert!(lift(bytes).is_err(), "{bytes:02x?}");
        }
    }
}
