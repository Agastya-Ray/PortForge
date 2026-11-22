use crate::address::Address;
use pf_decode::{DecodedOperandKind, Instruction, InstructionAttributes, Mnemonic, RegisterClass};
use std::fmt::Write;

#[derive(Debug, PartialEq, Eq)]
pub enum ScalarSse {
    Mxcsr {
        address: Address,
        store: bool,
    },
    Copy {
        destination: u8,
        source: u8,
        width: u16,
    },
    Load {
        destination: u8,
        address: Address,
        width: u16,
    },
    Store {
        address: Address,
        source: u8,
        width: u16,
    },
}

pub fn lift(instruction: &Instruction) -> Option<Result<ScalarSse, String>> {
    let mnemonic = instruction.mnemonic;
    let width = match mnemonic {
        Mnemonic::LDMXCSR | Mnemonic::STMXCSR | Mnemonic::MOVSS => 32,
        Mnemonic::MOVSD => 64,
        _ => return None,
    };
    let operands = instruction.visible_operands();
    // Zydis also names the operand-less string dword move MOVSD.
    if mnemonic == Mnemonic::MOVSD && operands.is_empty() {
        return None;
    }
    let unsupported = || format!("unsupported {mnemonic:?} SIMD operands");
    let lift = || {
        if instruction
            .attributes
            .intersects(InstructionAttributes::HAS_LOCK)
        {
            return Err(unsupported());
        }
        let address = |memory| {
            Address::for_access(memory, instruction.address_width, instruction.length)
                .ok_or_else(unsupported)
        };
        match operands {
            [operand] if matches!(mnemonic, Mnemonic::LDMXCSR | Mnemonic::STMXCSR) => {
                if let DecodedOperandKind::Mem(memory) = &operand.kind {
                    if operand.size == width {
                        return Ok(ScalarSse::Mxcsr {
                            address: address(memory)?,
                            store: mnemonic == Mnemonic::STMXCSR,
                        });
                    }
                }
                Err(unsupported())
            }
            [destination, source] if matches!(mnemonic, Mnemonic::MOVSS | Mnemonic::MOVSD) => {
                let xmm = |reg: pf_decode::Register| reg.class() == RegisterClass::XMM;
                match (&destination.kind, &source.kind) {
                    (DecodedOperandKind::Reg(dst), DecodedOperandKind::Reg(src))
                        if xmm(*dst) && xmm(*src) =>
                    {
                        Ok(ScalarSse::Copy {
                            destination: dst.id(),
                            source: src.id(),
                            width,
                        })
                    }
                    (DecodedOperandKind::Reg(dst), DecodedOperandKind::Mem(mem))
                        if xmm(*dst) && source.size == width =>
                    {
                        Ok(ScalarSse::Load {
                            destination: dst.id(),
                            address: address(mem)?,
                            width,
                        })
                    }
                    (DecodedOperandKind::Mem(mem), DecodedOperandKind::Reg(src))
                        if destination.size == width && xmm(*src) =>
                    {
                        Ok(ScalarSse::Store {
                            address: address(mem)?,
                            source: src.id(),
                            width,
                        })
                    }
                    _ => Err(unsupported()),
                }
            }
            _ => Err(unsupported()),
        }
    };
    Some(lift())
}

pub fn emit(operation: &ScalarSse, output: &mut String) {
    match operation {
        ScalarSse::Mxcsr { address, store } => {
            let address = address.c_expression();
            writeln!(
                output,
                "    if (!pf_mxcsr_memory(state, {address}, {store})) return;"
            )
            .unwrap();
        }
        ScalarSse::Copy {
            destination,
            source,
            width,
        } => {
            writeln!(
                output,
                "    pf_copy_xmm_scalar(state, {destination}, {source}, {width});"
            )
            .unwrap();
        }
        ScalarSse::Load {
            destination,
            address,
            width,
        } => {
            let address = address.c_expression();
            writeln!(output, "    if (!pf_xmm_scalar_memory(state, {address}, {destination}, {width}, false)) return;").unwrap();
        }
        ScalarSse::Store {
            address,
            source,
            width,
        } => {
            let address = address.c_expression();
            writeln!(
                output,
                "    if (!pf_xmm_scalar_memory(state, {address}, {source}, {width}, true)) return;"
            )
            .unwrap();
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::pfir::{lift, Effect};

    #[test]
    fn accepts_scalar_sse_and_rejects_vex_segmented_and_string_forms() {
        for bytes in [
            &[0x0f, 0xae, 0x10][..],         // LDMXCSR [RAX]
            &[0x67, 0x0f, 0xae, 0x18],       // STMXCSR [EAX]
            &[0xf3, 0x0f, 0x10, 0xc1],       // MOVSS XMM0, XMM1
            &[0xf2, 0x45, 0x0f, 0x11, 0xff], // MOVSD XMM15, XMM15
        ] {
            assert!(matches!(
                lift(bytes).unwrap().as_slice(),
                [Effect::ScalarSse(_), Effect::AdvanceRip(_)]
            ));
        }
        for bytes in [
            &[0x64, 0x0f, 0xae, 0x10][..],   // LDMXCSR FS:[RAX]
            &[0x65, 0xf2, 0x0f, 0x10, 0x00], // MOVSD XMM0, GS:[RAX]
            &[0xc5, 0xf8, 0xae, 0x10],       // VLDMXCSR [RAX]
            &[0xc5, 0xfa, 0x10, 0x00],       // VMOVSS XMM0, [RAX]
            &[0xf3, 0xa5],                   // REP MOVSD
        ] {
            assert!(lift(bytes).is_err());
        }
    }
}
