mod pe;

pub use pe::{PeDecodeError, PeDecoder};

use std::fmt;
use zydis::{AllOperands, StackWidth, Status};

pub use zydis::ffi::{DecodedOperand, DecodedOperandKind, MemoryInfo};
pub use zydis::{InstructionAttributes, MachineMode, Mnemonic, Register, RegisterClass};

pub type Instruction = zydis::Instruction<AllOperands>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    X86,
    X64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DecodeError {
    Truncated,
    Invalid(Status),
}

impl fmt::Display for DecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Truncated => formatter.write_str("truncated instruction"),
            Self::Invalid(status) => write!(formatter, "invalid instruction ({status})"),
        }
    }
}

impl std::error::Error for DecodeError {}

pub struct Decoder(zydis::Decoder);

impl Decoder {
    pub fn new(mode: Mode) -> Self {
        let decoder = match mode {
            Mode::X86 => zydis::Decoder::new(MachineMode::LEGACY_32, StackWidth::_32)
                .expect("valid 32-bit machine mode and stack width"),
            Mode::X64 => zydis::Decoder::new64(),
        };
        Self(decoder)
    }

    /// Decode one instruction. The caller supplies its mode and a bounded byte slice.
    pub fn decode(&self, bytes: &[u8]) -> Result<Instruction, DecodeError> {
        self.0
            .decode_first(bytes)
            .map_err(DecodeError::Invalid)?
            .ok_or(DecodeError::Truncated)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use zydis::Register;

    #[test]
    fn the_same_bytes_have_different_boundaries_in_x86_and_x64() {
        let x86 = Decoder::new(Mode::X86).decode(&[0x40, 0x90]).unwrap();
        let x64 = Decoder::new(Mode::X64).decode(&[0x40, 0x90]).unwrap();
        assert_eq!((x86.mnemonic, x86.length), (Mnemonic::INC, 1));
        assert_eq!((x64.mnemonic, x64.length), (Mnemonic::NOP, 2));
    }

    #[test]
    fn decodes_memory_and_instruction_families_outside_the_lifter() {
        let decoder = Decoder::new(Mode::X64);
        let mov = decoder
            .decode(&[0x48, 0x8b, 0x05, 0x78, 0x56, 0x34, 0x12])
            .unwrap();
        assert_eq!((mov.mnemonic, mov.length), (Mnemonic::MOV, 7));
        let DecodedOperandKind::Mem(memory) = &mov.visible_operands()[1].kind else {
            panic!("MOV has a RIP-relative source");
        };
        assert_eq!(memory.base, Register::RIP);
        assert_eq!(memory.disp.displacement, 0x12345678);

        for (bytes, mnemonic) in [
            (&[0xd9, 0xe8][..], Mnemonic::FLD1),
            (&[0x66, 0x0f, 0xef, 0xc0], Mnemonic::PXOR),
            (&[0xc5, 0xf8, 0x77], Mnemonic::VZEROUPPER),
        ] {
            let instruction = decoder.decode(bytes).unwrap();
            assert_eq!(instruction.mnemonic, mnemonic);
            assert_eq!(usize::from(instruction.length), bytes.len());
        }
    }

    #[test]
    fn distinguishes_truncated_bytes_from_invalid_encodings() {
        let decoder = Decoder::new(Mode::X64);
        assert_eq!(decoder.decode(&[0x48, 0x01]), Err(DecodeError::Truncated));
        assert!(matches!(
            decoder.decode(&[0xf0, 0x01, 0xc8]),
            Err(DecodeError::Invalid(_))
        ));
    }
}
