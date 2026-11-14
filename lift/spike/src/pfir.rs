use crate::address::Address;
use pf_decode::{
    DecodedOperandKind, Decoder, Instruction, MachineMode, Mnemonic, Mode, Register, RegisterClass,
};
use std::fmt::Write;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Width {
    U32,
    U64,
}

impl Width {
    fn from_gpr(class: RegisterClass) -> Option<Self> {
        match class {
            RegisterClass::GPR32 => Some(Self::U32),
            RegisterClass::GPR64 => Some(Self::U64),
            _ => None,
        }
    }

    fn bits(self) -> u8 {
        match self {
            Self::U32 => 32,
            Self::U64 => 64,
        }
    }

    fn from_bits(bits: u16) -> Option<Self> {
        match bits {
            32 => Some(Self::U32),
            64 => Some(Self::U64),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arithmetic {
    Add,
    Subtract,
    And,
    Or,
    Xor,
    Increment,
    Decrement,
    AddCarry,
    SubtractBorrow,
    Multiply,
}

impl Arithmetic {
    fn c_operator(self) -> char {
        match self {
            Self::Add | Self::Increment | Self::AddCarry => '+',
            Self::Subtract | Self::Decrement | Self::SubtractBorrow => '-',
            Self::Multiply => '*',
            Self::And => '&',
            Self::Or => '|',
            Self::Xor => '^',
        }
    }

    fn c_flags(self) -> &'static str {
        match self {
            Self::Add => "PF_ADD",
            Self::Subtract => "PF_SUB",
            Self::And | Self::Or | Self::Xor => "PF_LOGIC",
            Self::Increment => "PF_INC",
            Self::Decrement => "PF_DEC",
            Self::AddCarry => "PF_ADC",
            Self::SubtractBorrow => "PF_SBB",
            Self::Multiply => "PF_IMUL",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Packed {
    Xor,
    AddDwords,
    AddQwords,
    SubDwords,
    SubQwords,
    And,
    AndNot,
    Or,
    EqualDwords,
    GreaterSignedDwords,
}

impl Packed {
    fn c_operation(self) -> &'static str {
        match self {
            Self::Xor => "PF_PXOR",
            Self::AddDwords => "PF_PADDD",
            Self::AddQwords => "PF_PADDQ",
            Self::SubDwords => "PF_PSUBD",
            Self::SubQwords => "PF_PSUBQ",
            Self::And => "PF_PAND",
            Self::AndNot => "PF_PANDN",
            Self::Or => "PF_POR",
            Self::EqualDwords => "PF_PCMPEQD",
            Self::GreaterSignedDwords => "PF_PCMPGTD",
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
    Subtract,
    SubtractPop,
    ReverseSubtract,
    ReverseSubtractPop,
    Multiply,
    MultiplyPop,
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
            Self::Subtract => "PF_FSUB",
            Self::SubtractPop => "PF_FSUBP",
            Self::ReverseSubtract => "PF_FSUBR",
            Self::ReverseSubtractPop => "PF_FSUBRP",
            Self::Multiply => "PF_FMUL",
            Self::MultiplyPop => "PF_FMULP",
        }
    }
}

enum Operation {
    Scalar(Arithmetic, bool),
    Move,
    ConditionalMove,
    Packed(Packed),
    X87(X87),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Value(usize);

#[derive(Debug, PartialEq, Eq)]
pub enum NarrowLocation {
    Register { index: u8, shift: u8 },
    Memory(Address),
}

fn narrow_register(reg: Register) -> NarrowLocation {
    NarrowLocation::Register {
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

#[derive(Debug, PartialEq, Eq)]
pub enum Effect {
    ScalarStorage(crate::scalar_storage::ScalarStorage),
    ExtendNarrow {
        into: Value,
        source: NarrowLocation,
        source_width: u16,
        destination: u8,
        width: Width,
        signed: bool,
    },
    SetCondition {
        destination: NarrowLocation,
        condition: u8,
    },
    Read {
        into: Value,
        register: u8,
        width: Width,
    },
    Constant {
        into: Value,
        bits: u64,
        width: Width,
    },
    LoadScalar {
        into: Value,
        address: Address,
        width: Width,
    },
    StoreScalar {
        from: Value,
        address: Address,
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
    ConditionalWrite {
        register: u8,
        from: Value,
        width: Width,
        condition: u8,
    },
    Push(u8),
    Pop(u8),
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
    ShiftScalar {
        register: u8,
        width: Width,
        operation: &'static str,
        count: Option<u8>,
    },
    ShiftXmm {
        register: u8,
        operation: &'static str,
        count: u8,
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
    LoadX87Real {
        address: Address,
        width: Width,
    },
    ExtendedX87Memory {
        address: Address,
        store: bool,
    },
    LoadX87Integer {
        address: Address,
        width: u16,
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
    let mut offset = 0;
    lift_decoded(std::iter::from_fn(|| {
        let remaining = bytes.get(offset..).filter(|bytes| !bytes.is_empty())?;
        let result = decoder
            .decode(remaining)
            .map_err(|error| format!("{error} at offset {offset}"));
        offset += result
            .as_ref()
            .map_or(remaining.len(), |i| usize::from(i.length));
        Some(result)
    }))
}

pub fn lift_instruction(instruction: Instruction) -> Result<Vec<Effect>, String> {
    lift_decoded([Ok(instruction)])
}

fn lift_decoded(
    instructions: impl IntoIterator<Item = Result<Instruction, String>>,
) -> Result<Vec<Effect>, String> {
    let mut effects = Vec::new();
    let mut offset = 0;
    let mut next_value = 0;
    for instruction in instructions {
        let instruction = instruction?;
        let length = instruction.length;
        if let Some(storage) = crate::scalar_storage::decode(&instruction) {
            effects.push(Effect::ScalarStorage(
                storage.map_err(|e| format!("{e} at offset {offset}"))?,
            ));
            effects.push(Effect::AdvanceRip(length));
            offset += usize::from(length);
            continue;
        }
        if matches!(
            instruction.mnemonic,
            Mnemonic::MOVZX | Mnemonic::MOVSX | Mnemonic::MOVSXD
        ) {
            let unsupported = || format!("unsupported extension at offset {offset}");
            let [destination, source] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Reg(reg) = destination.kind else {
                return Err(unsupported());
            };
            let width = Width::from_gpr(reg.class()).ok_or_else(unsupported)?;
            if !matches!(source.size, 8 | 16 | 32) {
                return Err(unsupported());
            }
            let location = match &source.kind {
                DecodedOperandKind::Reg(reg)
                    if matches!(
                        reg.class(),
                        RegisterClass::GPR8 | RegisterClass::GPR16 | RegisterClass::GPR32
                    ) =>
                {
                    narrow_register(*reg)
                }
                DecodedOperandKind::Mem(memory) => NarrowLocation::Memory(
                    Address::for_access(memory, instruction.address_width, length)
                        .ok_or_else(unsupported)?,
                ),
                _ => return Err(unsupported()),
            };
            effects.extend([
                Effect::ExtendNarrow {
                    into: Value(next_value),
                    source: location,
                    source_width: source.size,
                    destination: reg.id(),
                    width,
                    signed: instruction.mnemonic != Mnemonic::MOVZX,
                },
                Effect::AdvanceRip(length),
            ]);
            next_value += 1;
            offset += usize::from(length);
            continue;
        }
        if matches!(
            instruction.mnemonic,
            Mnemonic::SETO
                | Mnemonic::SETNO
                | Mnemonic::SETB
                | Mnemonic::SETNB
                | Mnemonic::SETZ
                | Mnemonic::SETNZ
                | Mnemonic::SETBE
                | Mnemonic::SETNBE
                | Mnemonic::SETS
                | Mnemonic::SETNS
                | Mnemonic::SETP
                | Mnemonic::SETNP
                | Mnemonic::SETL
                | Mnemonic::SETNL
                | Mnemonic::SETLE
                | Mnemonic::SETNLE
        ) {
            let unsupported = || format!("unsupported condition result at offset {offset}");
            let [destination] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let location = match &destination.kind {
                DecodedOperandKind::Reg(reg) if reg.class() == RegisterClass::GPR8 => {
                    narrow_register(*reg)
                }
                DecodedOperandKind::Mem(memory) if destination.size == 8 => NarrowLocation::Memory(
                    Address::for_access(memory, instruction.address_width, length)
                        .ok_or_else(unsupported)?,
                ),
                _ => return Err(unsupported()),
            };
            effects.extend([
                Effect::SetCondition {
                    destination: location,
                    condition: instruction.opcode & 15,
                },
                Effect::AdvanceRip(length),
            ]);
            offset += usize::from(length);
            continue;
        }
        if instruction.mnemonic == Mnemonic::NOP {
            effects.push(Effect::AdvanceRip(length));
            offset += usize::from(length);
            continue;
        }
        if matches!(
            instruction.mnemonic,
            Mnemonic::PUSH
                | Mnemonic::POP
                | Mnemonic::INC
                | Mnemonic::DEC
                | Mnemonic::NEG
                | Mnemonic::NOT
        ) {
            let unsupported = || {
                format!(
                    "decoded {} at offset {offset}; unsupported operands",
                    instruction.mnemonic
                )
            };
            let [operand] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Reg(register) = operand.kind else {
                return Err(unsupported());
            };
            let width = Width::from_gpr(register.class()).ok_or_else(unsupported)?;
            let register = register.id();
            match instruction.mnemonic {
                Mnemonic::PUSH if width == Width::U64 => effects.push(Effect::Push(register)),
                Mnemonic::POP if width == Width::U64 => effects.push(Effect::Pop(register)),
                Mnemonic::INC | Mnemonic::DEC | Mnemonic::NEG | Mnemonic::NOT => {
                    let operation = match instruction.mnemonic {
                        Mnemonic::INC => Arithmetic::Increment,
                        Mnemonic::DEC => Arithmetic::Decrement,
                        Mnemonic::NEG => Arithmetic::Subtract,
                        _ => Arithmetic::Xor,
                    };
                    let lhs = Value(next_value);
                    let rhs = Value(next_value + 1);
                    let result = Value(next_value + 2);
                    effects.extend([
                        Effect::Read {
                            into: if instruction.mnemonic == Mnemonic::NEG {
                                rhs
                            } else {
                                lhs
                            },
                            register,
                            width,
                        },
                        Effect::Constant {
                            into: if instruction.mnemonic == Mnemonic::NEG {
                                lhs
                            } else {
                                rhs
                            },
                            bits: match instruction.mnemonic {
                                Mnemonic::NEG => 0,
                                Mnemonic::NOT => u64::MAX,
                                _ => 1,
                            },
                            width,
                        },
                        Effect::Binary {
                            into: result,
                            operation,
                            width,
                            lhs,
                            rhs,
                        },
                    ]);
                    if instruction.mnemonic != Mnemonic::NOT {
                        effects.push(Effect::PendingFlags {
                            operation,
                            width,
                            lhs,
                            rhs,
                            result,
                        });
                    }
                    effects.push(Effect::Write {
                        register,
                        from: result,
                    });
                    next_value += 3;
                }
                _ => return Err(unsupported()),
            }
            effects.push(Effect::AdvanceRip(length));
            offset += usize::from(length);
            continue;
        }
        if matches!(instruction.mnemonic, Mnemonic::FLD | Mnemonic::FSTP)
            && instruction
                .visible_operands()
                .first()
                .is_some_and(|operand| operand.size == 80)
        {
            let unsupported = || format!("unsupported extended x87 transfer at offset {offset}");
            let [operand] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Mem(memory) = &operand.kind else {
                return Err(unsupported());
            };
            let address = Address::for_access(memory, instruction.address_width, length)
                .ok_or_else(unsupported)?;
            effects.extend([
                Effect::ExtendedX87Memory {
                    address,
                    store: instruction.mnemonic == Mnemonic::FSTP,
                },
                Effect::AdvanceRip(length),
            ]);
            offset += usize::from(length);
            continue;
        }
        if instruction.mnemonic == Mnemonic::FILD {
            let unsupported = || format!("unsupported FILD operand at offset {offset}");
            let [operand] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Mem(memory) = &operand.kind else {
                return Err(unsupported());
            };
            if !matches!(operand.size, 16 | 32 | 64) {
                return Err(unsupported());
            }
            let address = Address::for_access(memory, instruction.address_width, length)
                .ok_or_else(unsupported)?;
            effects.extend([
                Effect::LoadX87Integer {
                    address,
                    width: operand.size,
                },
                Effect::AdvanceRip(length),
            ]);
            offset += usize::from(length);
            continue;
        }
        if instruction.mnemonic == Mnemonic::FLD {
            let error = || format!("unsupported FLD operand at offset {offset}");
            let [source] = instruction.visible_operands() else {
                return Err(error());
            };
            let DecodedOperandKind::Mem(memory) = &source.kind else {
                return Err(error());
            };
            let width = match source.size {
                32 => Width::U32,
                64 => Width::U64,
                _ => return Err(error()),
            };
            let length = instruction.length;
            let address =
                Address::for_access(memory, instruction.address_width, length).ok_or_else(error)?;
            effects.extend([
                Effect::LoadX87Real { address, width },
                Effect::AdvanceRip(length),
            ]);
            offset += usize::from(length);
            continue;
        }
        if matches!(
            instruction.mnemonic,
            Mnemonic::PSLLD | Mnemonic::PSLLQ | Mnemonic::PSRLD | Mnemonic::PSRLQ | Mnemonic::PSRAD
        ) {
            let unsupported = || format!("unsupported packed shift at offset {offset}");
            let [destination, source] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let (DecodedOperandKind::Reg(register), DecodedOperandKind::Imm(immediate)) =
                (&destination.kind, &source.kind)
            else {
                return Err(unsupported());
            };
            if register.class() != RegisterClass::XMM || immediate.is_relative {
                return Err(unsupported());
            }
            let count = u8::try_from(immediate.value).map_err(|_| unsupported())?;
            let operation = match instruction.mnemonic {
                Mnemonic::PSLLD => "PF_PSLLD",
                Mnemonic::PSLLQ => "PF_PSLLQ",
                Mnemonic::PSRLD => "PF_PSRLD",
                Mnemonic::PSRLQ => "PF_PSRLQ",
                _ => "PF_PSRAD",
            };
            effects.extend([
                Effect::ShiftXmm {
                    register: register.id(),
                    operation,
                    count,
                },
                Effect::AdvanceRip(length),
            ]);
            offset += usize::from(length);
            continue;
        }
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
        if matches!(
            instruction.mnemonic,
            Mnemonic::SHL | Mnemonic::SHR | Mnemonic::SAR
        ) {
            let unsupported = || format!("unsupported scalar shift at offset {offset}");
            let [destination, source] = instruction.visible_operands() else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Reg(reg) = destination.kind else {
                return Err(unsupported());
            };
            let width = Width::from_gpr(reg.class()).ok_or_else(unsupported)?;
            let count = match &source.kind {
                DecodedOperandKind::Imm(imm) if !imm.is_relative => {
                    Some(u8::try_from(imm.value).map_err(|_| unsupported())?)
                }
                DecodedOperandKind::Reg(reg) if *reg == pf_decode::Register::CL => None,
                _ => return Err(unsupported()),
            };
            let operation = match instruction.mnemonic {
                Mnemonic::SHL => "PF_SHL",
                Mnemonic::SHR => "PF_SHR",
                _ => "PF_SAR",
            };
            effects.extend([
                Effect::ShiftScalar {
                    register: reg.id(),
                    width,
                    operation,
                    count,
                },
                Effect::AdvanceRip(length),
            ]);
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
        if instruction.mnemonic == Mnemonic::IMUL && instruction.visible_operands().len() == 3 {
            let unsupported = || format!("unsupported three-operand IMUL at offset {offset}");
            let [destination, source, immediate] = instruction.visible_operands() else {
                unreachable!();
            };
            let DecodedOperandKind::Reg(register) = destination.kind else {
                return Err(unsupported());
            };
            let width = Width::from_gpr(register.class()).ok_or_else(unsupported)?;
            let lhs = Value(next_value);
            let rhs = Value(next_value + 1);
            let result = Value(next_value + 2);
            let read = match &source.kind {
                DecodedOperandKind::Reg(reg) if Width::from_gpr(reg.class()) == Some(width) => {
                    Effect::Read {
                        into: lhs,
                        register: reg.id(),
                        width,
                    }
                }
                DecodedOperandKind::Mem(memory) if source.size == u16::from(width.bits()) => {
                    Effect::LoadScalar {
                        into: lhs,
                        address: Address::for_access(memory, instruction.address_width, length)
                            .ok_or_else(unsupported)?,
                        width,
                    }
                }
                _ => return Err(unsupported()),
            };
            let DecodedOperandKind::Imm(imm) = &immediate.kind else {
                return Err(unsupported());
            };
            if imm.is_relative {
                return Err(unsupported());
            }
            effects.extend([
                read,
                Effect::Constant {
                    into: rhs,
                    bits: imm.value,
                    width,
                },
                Effect::Binary {
                    into: result,
                    operation: Arithmetic::Multiply,
                    width,
                    lhs,
                    rhs,
                },
                Effect::PendingFlags {
                    operation: Arithmetic::Multiply,
                    width,
                    lhs,
                    rhs,
                    result,
                },
                Effect::Write {
                    register: register.id(),
                    from: result,
                },
                Effect::AdvanceRip(length),
            ]);
            next_value += 3;
            offset += usize::from(length);
            continue;
        }
        let operation = match instruction.mnemonic {
            Mnemonic::ADD => Operation::Scalar(Arithmetic::Add, true),
            Mnemonic::ADC => Operation::Scalar(Arithmetic::AddCarry, true),
            Mnemonic::MOV => Operation::Move,
            Mnemonic::SUB => Operation::Scalar(Arithmetic::Subtract, true),
            Mnemonic::SBB => Operation::Scalar(Arithmetic::SubtractBorrow, true),
            Mnemonic::IMUL => Operation::Scalar(Arithmetic::Multiply, true),
            Mnemonic::CMP => Operation::Scalar(Arithmetic::Subtract, false),
            Mnemonic::TEST => Operation::Scalar(Arithmetic::And, false),
            Mnemonic::AND => Operation::Scalar(Arithmetic::And, true),
            Mnemonic::OR => Operation::Scalar(Arithmetic::Or, true),
            Mnemonic::XOR => Operation::Scalar(Arithmetic::Xor, true),
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
            | Mnemonic::CMOVNLE => Operation::ConditionalMove,
            Mnemonic::PXOR => Operation::Packed(Packed::Xor),
            Mnemonic::PADDD => Operation::Packed(Packed::AddDwords),
            Mnemonic::PADDQ => Operation::Packed(Packed::AddQwords),
            Mnemonic::PSUBD => Operation::Packed(Packed::SubDwords),
            Mnemonic::PSUBQ => Operation::Packed(Packed::SubQwords),
            Mnemonic::PAND => Operation::Packed(Packed::And),
            Mnemonic::PANDN => Operation::Packed(Packed::AndNot),
            Mnemonic::POR => Operation::Packed(Packed::Or),
            Mnemonic::PCMPEQD => Operation::Packed(Packed::EqualDwords),
            Mnemonic::PCMPGTD => Operation::Packed(Packed::GreaterSignedDwords),
            Mnemonic::FLD1 => Operation::X87(X87::LoadOne),
            Mnemonic::FLDZ => Operation::X87(X87::LoadZero),
            Mnemonic::FCHS => Operation::X87(X87::ChangeSign),
            Mnemonic::FABS => Operation::X87(X87::Absolute),
            Mnemonic::FADD => Operation::X87(X87::Add),
            Mnemonic::FADDP => Operation::X87(X87::AddPop),
            Mnemonic::FSUB => Operation::X87(X87::Subtract),
            Mnemonic::FSUBP => Operation::X87(X87::SubtractPop),
            Mnemonic::FSUBR => Operation::X87(X87::ReverseSubtract),
            Mnemonic::FSUBRP => Operation::X87(X87::ReverseSubtractPop),
            Mnemonic::FMUL => Operation::X87(X87::Multiply),
            Mnemonic::FMULP => Operation::X87(X87::MultiplyPop),
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
                (
                    X87::Add
                    | X87::AddPop
                    | X87::Subtract
                    | X87::SubtractPop
                    | X87::ReverseSubtract
                    | X87::ReverseSubtractPop
                    | X87::Multiply
                    | X87::MultiplyPop,
                    [destination, source],
                ) => {
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
        let length = instruction.length;
        if matches!(operation, Operation::Move) {
            let address = |memory| {
                Address::for_access(memory, instruction.address_width, length)
                    .ok_or_else(unsupported)
            };
            let value = Value(next_value);
            let (write, width) = match &destination.kind {
                DecodedOperandKind::Reg(reg) => (
                    Effect::Write {
                        register: reg.id(),
                        from: value,
                    },
                    Width::from_gpr(reg.class()).ok_or_else(unsupported)?,
                ),
                DecodedOperandKind::Mem(memory) => {
                    let width = Width::from_bits(destination.size).ok_or_else(unsupported)?;
                    (
                        Effect::StoreScalar {
                            from: value,
                            address: address(memory)?,
                            width,
                        },
                        width,
                    )
                }
                _ => return Err(unsupported()),
            };
            let read = match &source.kind {
                DecodedOperandKind::Reg(reg) if Width::from_gpr(reg.class()) == Some(width) => {
                    Effect::Read {
                        into: value,
                        register: reg.id(),
                        width,
                    }
                }
                DecodedOperandKind::Imm(imm) if !imm.is_relative => Effect::Constant {
                    into: value,
                    bits: imm.value,
                    width,
                },
                DecodedOperandKind::Mem(memory)
                    if matches!(write, Effect::Write { .. })
                        && source.size == u16::from(width.bits()) =>
                {
                    Effect::LoadScalar {
                        into: value,
                        address: address(memory)?,
                        width,
                    }
                }
                _ => return Err(unsupported()),
            };
            effects.extend([read, write, Effect::AdvanceRip(length)]);
            next_value += 1;
            offset += usize::from(length);
            continue;
        }
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
        let lhs = Value(next_value);
        let rhs = Value(next_value + 1);
        let result = Value(next_value + 2);
        match operation {
            Operation::Scalar(operation, writes) => {
                let width = Width::from_bits(destination.size).ok_or_else(unsupported)?;
                let address = |memory| {
                    Address::for_access(memory, instruction.address_width, length)
                        .ok_or_else(unsupported)
                };
                let (left, destination) = match &destination.kind {
                    DecodedOperandKind::Reg(reg) if Width::from_gpr(reg.class()) == Some(width) => {
                        (
                            Effect::Read {
                                into: lhs,
                                register: reg.id(),
                                width,
                            },
                            Some(reg.id()),
                        )
                    }
                    DecodedOperandKind::Mem(memory) if !writes => (
                        Effect::LoadScalar {
                            into: lhs,
                            address: address(memory)?,
                            width,
                        },
                        None,
                    ),
                    _ => return Err(unsupported()),
                };
                let right = match &source.kind {
                    DecodedOperandKind::Reg(reg) if Width::from_gpr(reg.class()) == Some(width) => {
                        Effect::Read {
                            into: rhs,
                            register: reg.id(),
                            width,
                        }
                    }
                    DecodedOperandKind::Imm(imm) if !imm.is_relative => Effect::Constant {
                        into: rhs,
                        bits: imm.value,
                        width,
                    },
                    DecodedOperandKind::Mem(memory) if source.size == u16::from(width.bits()) => {
                        Effect::LoadScalar {
                            into: rhs,
                            address: address(memory)?,
                            width,
                        }
                    }
                    _ => return Err(unsupported()),
                };
                effects.extend([
                    left,
                    right,
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
                if let Some(register) = destination.filter(|_| writes) {
                    effects.push(Effect::Write {
                        register,
                        from: result,
                    });
                }
            }
            Operation::ConditionalMove => {
                let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Reg(source)) =
                    (&destination.kind, &source.kind)
                else {
                    return Err(unsupported());
                };
                let width = Width::from_gpr(destination.class()).ok_or_else(unsupported)?;
                if Width::from_gpr(source.class()) != Some(width) {
                    return Err(unsupported());
                }
                effects.extend([
                    Effect::Read {
                        into: lhs,
                        register: source.id(),
                        width,
                    },
                    Effect::ConditionalWrite {
                        register: destination.id(),
                        from: lhs,
                        width,
                        condition: instruction.opcode & 15,
                    },
                ]);
            }
            Operation::Packed(operation) => {
                let (DecodedOperandKind::Reg(destination), DecodedOperandKind::Reg(source)) =
                    (&destination.kind, &source.kind)
                else {
                    return Err(unsupported());
                };
                if (destination.class(), source.class()) != (RegisterClass::XMM, RegisterClass::XMM)
                {
                    return Err(unsupported());
                }
                effects.extend([
                    Effect::ReadXmm {
                        into: lhs,
                        register: destination.id(),
                    },
                    Effect::ReadXmm {
                        into: rhs,
                        register: source.id(),
                    },
                    Effect::PackedBinary {
                        into: result,
                        operation,
                        lhs,
                        rhs,
                    },
                    Effect::WriteXmm {
                        register: destination.id(),
                        from: result,
                    },
                ]);
            }
            Operation::X87(_) | Operation::Move => unreachable!("operation lifted above"),
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
            Effect::ScalarStorage(ref instruction) => {
                crate::scalar_storage::emit(instruction, source)
            }
            Effect::ExtendNarrow {
                into: Value(id),
                source: ref location,
                source_width,
                destination,
                width,
                signed,
            } => {
                match location {
                    NarrowLocation::Register { index, shift } => writeln!(
                        source,
                        "    uint64_t v{id} = state->gpr[{index}] >> {shift};"
                    )
                    .unwrap(),
                    NarrowLocation::Memory(address) => {
                        let address = address.c_expression();
                        let bytes = source_width / 8;
                        writeln!(source, "    uint8_t *p{id};\n    if (!pf_guest_span(state, {address}, {bytes}, 1, false, &p{id})) return;").unwrap();
                        writeln!(source, "    uint64_t v{id} = 0;\n    for (unsigned i = 0; i < {bytes}; ++i) v{id} |= (uint64_t)p{id}[i] << (8 * i);").unwrap();
                    }
                }
                let mask = (1_u64 << source_width) - 1;
                writeln!(source, "    v{id} &= UINT64_C({mask:#x});").unwrap();
                if signed {
                    let sign = 1_u64 << (source_width - 1);
                    writeln!(
                        source,
                        "    if (v{id} & UINT64_C({sign:#x})) v{id} |= ~UINT64_C({mask:#x});"
                    )
                    .unwrap();
                }
                let width = width.bits();
                writeln!(
                    source,
                    "    state->gpr[{destination}] = (uint{width}_t)v{id};"
                )
                .unwrap();
            }
            Effect::SetCondition {
                ref destination,
                condition,
            } => match destination {
                NarrowLocation::Register { index, shift } => {
                    writeln!(source, "    state->gpr[{index}] = (state->gpr[{index}] & ~(UINT64_C(255) << {shift})) | ((uint64_t)pf_condition(pf_read_flags(state), {condition}) << {shift});").unwrap();
                }
                NarrowLocation::Memory(address) => {
                    let address = address.c_expression();
                    source.push_str("    { uint8_t *byte;\n");
                    writeln!(source, "      if (!pf_guest_span(state, {address}, 1, 1, true, &byte)) return;\n      *byte = pf_condition(pf_read_flags(state), {condition}); }}").unwrap();
                }
            },
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
            Effect::Constant {
                into: Value(id),
                bits,
                width,
            } => {
                let width = width.bits();
                writeln!(
                    source,
                    "    uint{width}_t v{id} = (uint{width}_t)UINT64_C({bits:#x});"
                )
                .unwrap();
            }
            Effect::LoadScalar {
                into: Value(id),
                ref address,
                width,
            } => {
                let bytes = width.bits() / 8;
                let width = width.bits();
                let address = address.c_expression();
                writeln!(source, "    uint8_t *p{id};").unwrap();
                writeln!(
                    source,
                    "    if (!pf_guest_span(state, {address}, {bytes}, 1, false, &p{id})) return;"
                )
                .unwrap();
                writeln!(
                    source,
                    "    uint{width}_t v{id} = pf_read_le{width}(p{id});"
                )
                .unwrap();
            }
            Effect::StoreScalar {
                from: Value(id),
                ref address,
                width,
            } => {
                let bytes = width.bits() / 8;
                let width = width.bits();
                let address = address.c_expression();
                writeln!(source, "    uint8_t *p{id};").unwrap();
                writeln!(
                    source,
                    "    if (!pf_guest_span(state, {address}, {bytes}, 1, true, &p{id})) return;"
                )
                .unwrap();
                writeln!(source, "    pf_write_le{width}(p{id}, v{id});").unwrap();
            }
            Effect::Binary {
                into: Value(id),
                operation,
                width,
                lhs: Value(lhs),
                rhs: Value(rhs),
            } => {
                let operator = operation.c_operator();
                let width = width.bits();
                let carry =
                    if matches!(operation, Arithmetic::AddCarry | Arithmetic::SubtractBorrow) {
                        format!(" {operator} (pf_read_flags(state) & UINT64_C(1))")
                    } else {
                        String::new()
                    };
                writeln!(
                    source,
                    "    uint{width}_t v{id} = v{lhs} {operator} v{rhs}{carry};"
                )
                .unwrap();
            }
            Effect::Write {
                register,
                from: Value(id),
            } => {
                writeln!(source, "    state->gpr[{register}] = v{id};").unwrap();
            }
            Effect::ConditionalWrite {
                register,
                from: Value(id),
                width,
                condition,
            } => {
                let width = width.bits();
                writeln!(source, "    state->gpr[{register}] = pf_condition(pf_read_flags(state), {condition}) ? v{id} : (uint{width}_t)state->gpr[{register}];").unwrap();
            }
            Effect::Push(register) => {
                writeln!(
                    source,
                    "    if (!pf_push(state, state->gpr[{register}])) return;"
                )
                .unwrap();
            }
            Effect::Pop(register) => {
                writeln!(source, "    if (!pf_pop(state, {register})) return;").unwrap();
            }
            Effect::PendingFlags {
                operation,
                width,
                lhs: Value(lhs),
                rhs: Value(rhs),
                result: Value(id),
            } => {
                let mode = operation.c_flags();
                if matches!(
                    operation,
                    Arithmetic::AddCarry | Arithmetic::SubtractBorrow | Arithmetic::Multiply
                ) {
                    source.push_str("    state->rflags = pf_read_flags(state);\n");
                }
                if matches!(operation, Arithmetic::Increment | Arithmetic::Decrement) {
                    source.push_str("    state->rflags = (state->rflags & ~UINT64_C(1)) | (pf_read_flags(state) & UINT64_C(1));\n");
                }
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
            Effect::ShiftScalar {
                register,
                width,
                operation,
                count,
            } => {
                let width = width.bits();
                let count =
                    count.map_or_else(|| "state->gpr[1]".to_owned(), |count| count.to_string());
                writeln!(
                    source,
                    "    pf_shift_scalar(state, {register}, {width}, {operation}, {count});"
                )
                .unwrap();
            }
            Effect::ShiftXmm {
                register,
                operation,
                count,
            } => {
                writeln!(source, "    state->xmm[{register}] = pf_xmm_shift({operation}, state->xmm[{register}], {count});").unwrap();
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
            Effect::ExtendedX87Memory { ref address, store } => {
                let address = address.c_expression();
                writeln!(
                    source,
                    "    if (!pf_x87_extended_memory(state, {address}, {store})) return;"
                )
                .unwrap();
            }
            Effect::LoadX87Real { ref address, width } => {
                let expression = address.c_expression();
                let width = width.bits();
                writeln!(
                    source,
                    "    if (!pf_load_x87_real(state, {expression}, {width})) return;"
                )
                .unwrap();
            }
            Effect::LoadX87Integer { ref address, width } => {
                let expression = address.c_expression();
                writeln!(
                    source,
                    "    if (!pf_load_x87_integer(state, {expression}, {width})) return;"
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
    fn integer_expansion_rejects_implicit_products_and_unsupported_storage() {
        for bytes in [
            &[0xf7, 0xe9][..],               // IMUL ECX, implicit EDX:EAX
            &[0x48, 0xf7, 0xe9],             // IMUL RCX, implicit RDX:RAX
            &[0x66, 0x0f, 0xaf, 0xc1],       // IMUL AX, CX
            &[0x66, 0x6b, 0xc1, 0xff],       // IMUL AX, CX, -1
            &[0x10, 0xc8],                   // ADC AL, CL
            &[0x66, 0x11, 0xc8],             // ADC AX, CX
            &[0x18, 0xc8],                   // SBB AL, CL
            &[0x66, 0x19, 0xc8],             // SBB AX, CX
            &[0xf6, 0xd8],                   // NEG AL
            &[0x66, 0xf7, 0xd0],             // NOT AX
            &[0x48, 0x11, 0x08],             // ADC [RAX], RCX
            &[0x48, 0x19, 0x08],             // SBB [RAX], RCX
            &[0xf7, 0x18],                   // NEG dword [RAX]
            &[0xf7, 0x10],                   // NOT dword [RAX]
            &[0xf0, 0x48, 0x11, 0x08],       // LOCK ADC [RAX], RCX
            &[0x64, 0x48, 0x13, 0x02],       // ADC RAX, FS:[RDX]
            &[0x65, 0x48, 0x0f, 0xaf, 0x02], // IMUL RAX, GS:[RDX]
            &[0x65, 0x48, 0x6b, 0x02, 0xff], // IMUL RAX, GS:[RDX], -1
        ] {
            let instruction = Decoder::new(Mode::X64).decode(bytes).unwrap();
            let storage = matches!(crate::scalar_storage::decode(&instruction), Some(Ok(_)));
            assert_eq!(
                lift(bytes).is_ok(),
                storage,
                "unexpected lift for {bytes:02x?}"
            );
        }
    }

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
        for bytes in [&[0x48][..], &[0x01], &[0x66, 0x50], &[0xf0, 0x01, 0xc8]] {
            assert!(lift(bytes).is_err(), "{bytes:02x?}");
        }
    }
    #[test]
    fn distinguishes_decode_failure_from_missing_semantics() {
        assert!(lift(&[0x48, 0x01]).unwrap_err().contains("truncated"));
        assert!(lift(&[0xd9, 0xfa]).unwrap_err().contains("decoded fsqrt"));
        assert!(lift(&[0x66, 0x50])
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

    #[test]
    fn x87_memory_lifts_flat_real_values() {
        for (bytes, width) in [([0xd9, 0x00], Width::U32), ([0xdd, 0x00], Width::U64)] {
            let effects = lift(&bytes).unwrap();
            assert!(matches!(
                effects.as_slice(),
                [Effect::LoadX87Real { width: actual, .. }, Effect::AdvanceRip(2)]
                    if *actual == width
            ));
        }
        for bytes in [&[0xd9, 0xc0][..], &[0x64, 0xd9, 0x00]] {
            assert!(lift(bytes).is_err(), "{bytes:02x?}");
        }
    }
}
