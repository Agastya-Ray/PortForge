use pf_decode::{MemoryInfo, Register, RegisterClass};

#[derive(Debug, PartialEq, Eq)]
pub struct Address {
    base: Register,
    index: Register,
    scale: u8,
    displacement: i64,
    width: u8,
    length: u8,
}

impl Address {
    pub fn new(memory: &MemoryInfo, width: u8, length: u8) -> Option<Self> {
        let gpr =
            |reg: Register| matches!(reg.class(), RegisterClass::GPR32 | RegisterClass::GPR64);
        if !matches!(width, 32 | 64)
            || !(matches!(memory.base, Register::NONE | Register::RIP | Register::EIP)
                || gpr(memory.base))
            || (memory.index != Register::NONE
                && (!gpr(memory.index) || !matches!(memory.scale, 1 | 2 | 4 | 8)))
        {
            return None;
        }
        Some(Self {
            base: memory.base,
            index: memory.index,
            scale: memory.scale,
            displacement: memory.disp.displacement,
            width,
            length,
        })
    }

    pub fn c_expression(&self) -> String {
        let base = match self.base {
            Register::NONE => "UINT64_C(0)".into(),
            Register::RIP | Register::EIP => format!("(state->rip + {})", self.length),
            reg => format!("state->gpr[{}]", reg.id()),
        };
        let index = if self.index == Register::NONE {
            "UINT64_C(0)".into()
        } else {
            format!("state->gpr[{}] * {}", self.index.id(), self.scale)
        };
        format!(
            "(uint{}_t)({base} + {index} + UINT64_C({:#x}))",
            self.width, self.displacement as u64
        )
    }
}
