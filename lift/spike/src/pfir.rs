use crate::address::Address;
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
pub enum Packed {
    Xor,
    AddDwords,
    AddQwords,
    SubDwords,
    SubQwords,
}

impl Packed {
    fn c_operation(self) -> &'static str {
        match self {
            Self::Xor => "PF_PXOR",
            Self::AddDwords => "PF_PADDD",
            Self::AddQwords => "PF_PADDQ",
            Self::SubDwords => "PF_PSUBD",
            Self::SubQwords => "PF_PSUBQ",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum X87 {
    LoadOne,
    LoadZero,
    ChangeSign,
    Absolute,
    Add,
    AddPop,
}

impl X87 {
    fn c_operation(self) -> &'static str {
        match self {
            Self::LoadOne => "PF_FLD1",
            Self::LoadZero => "PF_FLDZ",
            Self::ChangeSign => "PF_FCHS",
            Self::Absolute => "PF_FABS",
            Self::Add => "PF_FADD",
            Self::AddPop => "PF_FADDP",
        }
    }
}

enum Operation {
    Scalar(Arithmetic, bool),
    Packed(Packed),
    X87(X87),
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
    ReadXmm {
        into: Value,
        register: u8,
    },
    PackedBinary {
        into: Value,
        operation: Packed,
        lhs: Value,
        rhs: Value,
    },
    WriteXmm {
        register: u8,
        from: Value,
    },
    X87 {
        operation: X87,
        destination: u8,
        source: u8,
    },
    LoadXmm {
        into: Value,
        address: Address,
        alignment: u8,
    },
    StoreXmm {
        from: Value,
        address: Address,
        alignment: u8,
    },
    EffectiveAddress {
        into: Value,
        address: Address,
        width: Width,
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
        if matches!(instruction.mnemonic, Mnemonic::MOVDQA | Mnemonic::MOVDQU) {
            let alignment = if instruction.mnemonic == Mnemonic::MOVDQA {
                16
            } else {
                1
            };
            let length = instruction.length;
            let error = || format!("unsupported XMM move operands at offset {offset}");
            let [destination, source] = instruction.visible_operands() else {
                return Err(error());
            };
            let value = Value(next_value);
            let address = |memory| {
                Address::for_access(memory, instruction.address_width, length).ok_or_else(error)
            };
            let read = match &source.kind {
                DecodedOperandKind::Reg(reg) if reg.class() == RegisterClass::XMM => {
                    Effect::ReadXmm {
                        into: value,
                        register: reg.id(),
                    }
                }
                DecodedOperandKind::Mem(memory) if source.size == 128 => Effect::LoadXmm {
                    into: value,
                    address: address(memory)?,
                    alignment,
                },
                _ => return Err(error()),
            };
            let write = match &destination.kind {
                DecodedOperandKind::Reg(reg) if reg.class() == RegisterClass::XMM => {
                    Effect::WriteXmm {
                        register: reg.id(),
                        from: value,
                    }
                }
                DecodedOperandKind::Mem(memory) if destination.size == 128 => Effect::StoreXmm {
                    from: value,
                    address: address(memory)?,
                    alignment,
                },
                _ => return Err(error()),
            };
            effects.extend([read, write, Effect::AdvanceRip(length)]);
            next_value += 1;
            offset += usize::from(length);
            continue;
        }
        if instruction.mnemonic == Mnemonic::LEA {
            let error = || format!("unsupported LEA operands at offset {offset}");
            let [destination, memory] = instruction.visible_operands() else {
                return Err(error());
            };
            let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Mem(memory)) =
                (&destination.kind, &memory.kind)
            else {
                return Err(error());
            };
            let width = match destination.class() {
                RegisterClass::GPR32 => Width::U32,
                RegisterClass::GPR64 => Width::U64,
                _ => return Err(error()),
            };
            let length = instruction.length;
            let address =
                Address::new(memory, instruction.address_width, length).ok_or_else(error)?;
            let into = Value(next_value);
            effects.extend([
                Effect::EffectiveAddress {
                    into,
                    address,
                    width,
                },
                Effect::Write {
                    register: destination.id(),
                    from: into,
                },
                Effect::AdvanceRip(length),
            ]);
            next_value += 1;
            offset += usize::from(length);
            continue;
        }
        let operation = match instruction.mnemonic {
            Mnemonic::ADD => Operation::Scalar(Arithmetic::Add, true),
            Mnemonic::SUB => Operation::Scalar(Arithmetic::Subtract, true),
            Mnemonic::CMP => Operation::Scalar(Arithmetic::Subtract, false),
            Mnemonic::PXOR => Operation::Packed(Packed::Xor),
            Mnemonic::PADDD => Operation::Packed(Packed::AddDwords),
            Mnemonic::PADDQ => Operation::Packed(Packed::AddQwords),
            Mnemonic::PSUBD => Operation::Packed(Packed::SubDwords),
            Mnemonic::PSUBQ => Operation::Packed(Packed::SubQwords),
            Mnemonic::FLD1 => Operation::X87(X87::LoadOne),
            Mnemonic::FLDZ => Operation::X87(X87::LoadZero),
            Mnemonic::FCHS => Operation::X87(X87::ChangeSign),
            Mnemonic::FABS => Operation::X87(X87::Absolute),
            Mnemonic::FADD => Operation::X87(X87::Add),
            Mnemonic::FADDP => Operation::X87(X87::AddPop),
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
        if let Operation::X87(operation) = operation {
            let (destination, source) = match (operation, instruction.visible_operands()) {
                (X87::Add | X87::AddPop, [destination, source]) => {
                    let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Reg(source)) =
                        (&destination.kind, &source.kind)
                    else {
                        return Err(unsupported());
                    };
                    if destination.class() != RegisterClass::X87
                        || source.class() != RegisterClass::X87
                    {
                        return Err(unsupported());
                    }
                    (destination.id(), source.id())
                }
                (X87::LoadOne | X87::LoadZero | X87::ChangeSign | X87::Absolute, []) => (0, 0),
                _ => return Err(unsupported()),
            };
            effects.push(Effect::X87 {
                operation,
                destination,
                source,
            });
            effects.push(Effect::AdvanceRip(instruction.length));
            offset += usize::from(instruction.length);
            continue;
        }
        let [destination, source] = instruction.visible_operands() else {
            return Err(unsupported());
        };
        if let (
            Operation::Packed(operation),
            DecodedOperandKind::Reg(destination),
            DecodedOperandKind::Mem(memory),
        ) = (&operation, &destination.kind, &source.kind)
        {
            if destination.class() != RegisterClass::XMM || source.size != 128 {
                return Err(unsupported());
            }
            let length = instruction.length;
            let address = Address::for_access(memory, instruction.address_width, length)
                .ok_or_else(unsupported)?;
            let lhs = Value(next_value);
            let rhs = Value(next_value + 1);
            let result = Value(next_value + 2);
            effects.extend([
                Effect::ReadXmm {
                    into: lhs,
                    register: destination.id(),
                },
                Effect::LoadXmm {
                    into: rhs,
                    address,
                    alignment: 16,
                },
                Effect::PackedBinary {
                    into: result,
                    operation: *operation,
                    lhs,
                    rhs,
                },
                Effect::WriteXmm {
                    register: destination.id(),
                    from: result,
                },
                Effect::AdvanceRip(length),
            ]);
            next_value += 3;
            offset += usize::from(length);
            continue;
        }
        let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Reg(source)) =
            (&destination.kind, &source.kind)
        else {
            return Err(unsupported());
        };
        let classes = (destination.class(), source.class());
        let (destination, source) = (destination.id(), source.id());
        let lhs = Value(next_value);
        let rhs = Value(next_value + 1);
        let result = Value(next_value + 2);
        let length = instruction.length;
        match operation {
            Operation::Scalar(operation, writes) => {
                let width = match classes {
                    (RegisterClass::GPR32, RegisterClass::GPR32) => Width::U32,
                    (RegisterClass::GPR64, RegisterClass::GPR64) => Width::U64,
                    _ => return Err(unsupported()),
                };
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
            }
            Operation::Packed(operation) => {
                if classes != (RegisterClass::XMM, RegisterClass::XMM) {
                    return Err(unsupported());
                }
                effects.extend([
                    Effect::ReadXmm {
                        into: lhs,
                        register: destination,
                    },
                    Effect::ReadXmm {
                        into: rhs,
                        register: source,
                    },
                    Effect::PackedBinary {
                        into: result,
                        operation,
                        lhs,
                        rhs,
                    },
                    Effect::WriteXmm {
                        register: destination,
                        from: result,
                    },
                ]);
            }
            Operation::X87(_) => unreachable!("x87 lifted above"),
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
            Effect::ReadXmm {
                into: Value(id),
                register,
            } => {
                writeln!(source, "    struct pf_xmm v{id} = state->xmm[{register}];").unwrap();
            }
            Effect::PackedBinary {
                into: Value(id),
                operation,
                lhs: Value(lhs),
                rhs: Value(rhs),
            } => {
                let operation = operation.c_operation();
                writeln!(
                    source,
                    "    struct pf_xmm v{id} = pf_packed({operation}, v{lhs}, v{rhs});"
                )
                .unwrap();
            }
            Effect::WriteXmm {
                register,
                from: Value(id),
            } => {
                writeln!(source, "    state->xmm[{register}] = v{id};").unwrap();
            }
            Effect::X87 {
                operation,
                destination,
                source: operand,
            } => {
                let operation = operation.c_operation();
                writeln!(source, "    if (!pf_x87_execute(&state->x87, {operation}, {destination}, {operand})) {{").unwrap();
                writeln!(source, "        state->x87_unsupported = true;").unwrap();
                writeln!(source, "        return;").unwrap();
                writeln!(source, "    }}").unwrap();
            }
            Effect::LoadXmm {
                into: Value(id),
                ref address,
                alignment,
            } => {
                let expression = address.c_expression();
                writeln!(source, "    struct pf_xmm v{id};").unwrap();
                writeln!(
                    source,
                    "    if (!pf_load_xmm(state, {expression}, {alignment}, &v{id})) return;"
                )
                .unwrap();
            }
            Effect::StoreXmm {
                from: Value(id),
                ref address,
                alignment,
            } => {
                let expression = address.c_expression();
                writeln!(
                    source,
                    "    if (!pf_store_xmm(state, {expression}, {alignment}, v{id})) return;"
                )
                .unwrap();
            }
            Effect::EffectiveAddress {
                into: Value(id),
                ref address,
                width,
            } => {
                let expression = address.c_expression();
                let width = width.bits();
                writeln!(
                    source,
                    "    uint{width}_t v{id} = (uint{width}_t){expression};"
                )
                .unwrap();
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
    fn every_register_pair_maps_to_the_expected_guest_state_slots() {
        for (opcodes, operation, writes) in [
            ([0x01, 0x03], Arithmetic::Add, true),
            ([0x29, 0x2b], Arithmetic::Subtract, true),
            ([0x39, 0x3b], Arithmetic::Subtract, false),
        ] {
            for (width, rex_width) in [(Width::U32, 0), (Width::U64, 8)] {
                for destination in 0_u8..16 {
                    for source in 0_u8..16 {
                        for (direction, opcode) in opcodes.into_iter().enumerate() {
                            let (reg, rm) = if direction == 0 {
                                (source, destination)
                            } else {
                                (destination, source)
                            };
                            let rex = 0x40 | rex_width | ((reg >> 3) << 2) | (rm >> 3);
                            let bytes = [rex, opcode, 0xc0 | ((reg & 7) << 3) | (rm & 7)];
                            let encoding = if rex == 0x40 { &bytes[1..] } else { &bytes[..] };
                            let effects = lift(encoding).unwrap();
                            assert_eq!(
                                effects[0],
                                Effect::Read {
                                    into: Value(0),
                                    register: destination,
                                    width
                                }
                            );
                            assert_eq!(
                                effects[1],
                                Effect::Read {
                                    into: Value(1),
                                    register: source,
                                    width
                                }
                            );
                            assert_eq!(
                                effects[2],
                                Effect::Binary {
                                    into: Value(2),
                                    operation,
                                    width,
                                    lhs: Value(0),
                                    rhs: Value(1)
                                }
                            );
                            if writes {
                                assert_eq!(
                                    effects[4],
                                    Effect::Write {
                                        register: destination,
                                        from: Value(2)
                                    }
                                );
                            }
                            assert_eq!(effects.len(), if writes { 6 } else { 5 });
                            assert_eq!(
                                effects.last(),
                                Some(&Effect::AdvanceRip(encoding.len() as u8))
                            );
                        }
                    }
                }
            }
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
        assert!(lift(&[0xd9, 0xfa]).unwrap_err().contains("decoded fsqrt"));
        assert!(lift(&[0x01, 0x08])
            .unwrap_err()
            .contains("unsupported operands"));
    }
    #[test]
    fn packed_lifts_reject_mmx_segmented_memory_and_vex_forms() {
        for encoding in [
            &[0x0f, 0xef, 0xc1][..],         // PXOR MM0, MM1
            &[0x64, 0x66, 0x0f, 0xef, 0x01], // PXOR XMM0, FS:[RCX]
            &[0xc5, 0xf9, 0xef, 0xc1],       // VPXOR XMM0, XMM0, XMM1
        ] {
            assert!(lift(encoding).is_err(), "{encoding:02x?}");
        }
    }
    #[test]
    fn lifts_both_x87_add_directions_and_rejects_memory() {
        for (bytes, operation, destination, source) in [
            ([0xd8, 0xc3], X87::Add, 0, 3),
            ([0xdc, 0xc3], X87::Add, 3, 0),
            ([0xde, 0xc3], X87::AddPop, 3, 0),
        ] {
            assert_eq!(
                lift(&bytes).unwrap(),
                [
                    Effect::X87 {
                        operation,
                        destination,
                        source
                    },
                    Effect::AdvanceRip(2)
                ]
            );
        }
        assert!(lift(&[0xd8, 0x00]).is_err());
    }
}
