use crate::address::Address;
use pf_decode::{DecodedOperandKind, Instruction, InstructionAttributes, Mnemonic, Register};
use std::fmt::Write;

#[derive(Debug, PartialEq, Eq)]
pub enum Control {
    Initialize,
    Clear,
    Load(Address),
    Store(Address, bool),
    StatusAx,
}

pub fn lift(instruction: &Instruction) -> Result<Option<Control>, String> {
    let mnemonic = instruction.mnemonic;
    if !matches!(
        mnemonic,
        Mnemonic::FNINIT | Mnemonic::FNCLEX | Mnemonic::FLDCW | Mnemonic::FNSTCW | Mnemonic::FNSTSW
    ) {
        return Ok(None);
    }
    let unsupported = || format!("unsupported {mnemonic:?} control operand");
    if instruction
        .attributes
        .intersects(InstructionAttributes::HAS_LOCK)
    {
        return Err(unsupported());
    }
    let operands = instruction.visible_operands();
    match mnemonic {
        Mnemonic::FNINIT if operands.is_empty() => Ok(Some(Control::Initialize)),
        Mnemonic::FNCLEX if operands.is_empty() => Ok(Some(Control::Clear)),
        Mnemonic::FNSTSW if matches!(operands, [operand] if operand.kind == DecodedOperandKind::Reg(Register::AX)) => {
            Ok(Some(Control::StatusAx))
        }
        _ => {
            let [operand] = operands else {
                return Err(unsupported());
            };
            let DecodedOperandKind::Mem(memory) = &operand.kind else {
                return Err(unsupported());
            };
            if operand.size != 16 {
                return Err(unsupported());
            }
            let address =
                Address::for_access(memory, instruction.address_width, instruction.length)
                    .ok_or_else(unsupported)?;
            Ok(Some(if mnemonic == Mnemonic::FLDCW {
                Control::Load(address)
            } else {
                Control::Store(address, mnemonic == Mnemonic::FNSTSW)
            }))
        }
    }
}

pub fn emit(control: &Control, source: &mut String) {
    source.push_str("    {\n");
    match control {
        Control::Initialize => source.push_str(
            "    state->x87.control = 0x037f; state->x87.status = 0; state->x87.occupied = 0;\n",
        ),
        Control::Clear => source.push_str("    state->x87.status &= ~UINT16_C(0x80ff);\n"),
        Control::StatusAx => source.push_str(
            "    state->gpr[0] = (state->gpr[0] & ~UINT64_C(0xffff)) | state->x87.status;\n",
        ),
        Control::Load(address) => {
            source.push_str("    if (state->x87.status & PF_X87_ERROR_SUMMARY) { state->x87_unsupported = true; return; }\n");
            writeln!(source, "    uint8_t *control; if (!pf_guest_span(state, {}, 2, 1, false, &control)) return;", address.c_expression()).unwrap();
            source.push_str("    state->x87.control = (((uint16_t)control[0] | (uint16_t)control[1] << 8) & 0x1f3f) | 0x40;\n");
            source.push_str("    if (state->x87.status & ~state->x87.control & 0x3f) state->x87.status |= 0x8080;\n    else state->x87.status &= ~UINT16_C(0x8080);\n");
        }
        Control::Store(address, status) => {
            writeln!(source, "    uint8_t *control; if (!pf_guest_span(state, {}, 2, 1, true, &control)) return;", address.c_expression()).unwrap();
            let field = if *status { "status" } else { "control" };
            writeln!(source, "    control[0] = (uint8_t)state->x87.{field}; control[1] = (uint8_t)(state->x87.{field} >> 8);").unwrap();
        }
    }
    source.push_str("    }\n");
}
