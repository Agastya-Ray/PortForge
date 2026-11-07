use crate::{DecodeError, Decoder, Instruction, Mode};
use pf_detect::pe::{Architecture, Pe};
use std::fmt;

#[derive(Debug, PartialEq, Eq)]
pub enum PeDecodeError {
    NoExecutableBytes { rva: u32 },
    Instruction { rva: u32, error: DecodeError },
}

impl fmt::Display for PeDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NoExecutableBytes { rva } => {
                write!(formatter, "no file-backed executable bytes at RVA {rva:#x}")
            }
            Self::Instruction { rva, error } => write!(formatter, "{error} at RVA {rva:#x}"),
        }
    }
}

impl std::error::Error for PeDecodeError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Instruction { error, .. } => Some(error),
            Self::NoExecutableBytes { .. } => None,
        }
    }
}

pub struct PeDecoder<'pe, 'file> {
    image: &'pe Pe<'file>,
    decoder: Decoder,
}

impl<'pe, 'file> PeDecoder<'pe, 'file> {
    pub fn new(image: &'pe Pe<'file>) -> Self {
        let mode = match image.arch {
            Architecture::X86 => Mode::X86,
            Architecture::X64 => Mode::X64,
        };
        Self {
            image,
            decoder: Decoder::new(mode),
        }
    }

    /// Decode one instruction from the original PE bytes at a guest RVA.
    pub fn decode_at(&self, rva: u32) -> Result<Instruction, PeDecodeError> {
        let mut bytes = [0; zydis::MAX_INSTRUCTION_LENGTH];
        let size = self.image.read_executable(rva, &mut bytes);
        if size == 0 {
            return Err(PeDecodeError::NoExecutableBytes { rva });
        }
        self.decoder
            .decode(&bytes[..size])
            .map_err(|error| PeDecodeError::Instruction { rva, error })
    }
}
