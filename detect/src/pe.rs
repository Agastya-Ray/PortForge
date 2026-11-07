use std::io::{Error, ErrorKind, Result};

fn invalid(message: &str) -> Error {
    Error::new(ErrorKind::InvalidData, message)
}

#[derive(Clone, Copy)]
struct Bytes<'a>(&'a [u8]);

impl<'a> Bytes<'a> {
    fn at(self, offset: usize, size: usize) -> Result<Self> {
        offset
            .checked_add(size)
            .and_then(|end| self.0.get(offset..end))
            .map(Self)
            .ok_or_else(|| invalid("truncated PE structure"))
    }

    fn word(self, offset: usize) -> Result<u16> {
        Ok(u16::from_le_bytes(
            self.at(offset, 2)?.0.try_into().unwrap(),
        ))
    }

    fn dword(self, offset: usize) -> Result<u32> {
        Ok(u32::from_le_bytes(
            self.at(offset, 4)?.0.try_into().unwrap(),
        ))
    }

    fn qword(self, offset: usize) -> Result<u64> {
        Ok(u64::from_le_bytes(
            self.at(offset, 8)?.0.try_into().unwrap(),
        ))
    }
}

#[derive(Debug, Clone, Copy)]
pub enum Architecture {
    X86,
    X64,
}

impl Architecture {
    pub fn name(self) -> &'static str {
        match self {
            Self::X86 => "x86",
            Self::X64 => "x64",
        }
    }

    fn pointer_size(self) -> usize {
        match self {
            Self::X86 => 4,
            Self::X64 => 8,
        }
    }
}

#[derive(Debug)]
pub enum Symbol {
    Name(String),
    Ordinal(u16),
}

#[derive(Debug)]
pub struct Import {
    pub dll: String,
    pub symbols: Vec<Symbol>,
    pub delayed: bool,
}

#[derive(Clone, Copy)]
struct Section {
    rva: u32,
    virtual_size: u32,
    raw: u32,
    raw_size: u32,
}

pub struct Pe<'a> {
    file: Bytes<'a>,
    sections: Vec<Section>,
    directories: Bytes<'a>,
    pub arch: Architecture,
    pub is_dll: bool,
    pub entry_rva: u32,
    pub image_base: u64,
    pub image_size: u32,
    headers: u32,
}

impl<'a> Pe<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self> {
        let file = Bytes(data);
        if file.at(0, 2)?.0 != b"MZ" {
            return Err(invalid("missing MZ signature"));
        }
        let pe = file.dword(0x3c)? as usize;
        if file.at(pe, 4)?.0 != b"PE\0\0" {
            return Err(invalid("missing PE signature"));
        }
        let coff = file.at(pe + 4, 20)?;
        let optional_size = usize::from(coff.word(16)?);
        let opt = file.at(pe + 24, optional_size)?;
        let (arch, base, count_offset) = match (coff.word(0)?, opt.word(0)?) {
            (0x14c, 0x10b) => (Architecture::X86, u64::from(opt.dword(28)?), 92),
            (0x8664, 0x20b) => (Architecture::X64, opt.qword(24)?, 108),
            _ => return Err(invalid("expected x86 PE32 or x64 PE32+")),
        };
        let count = opt.dword(count_offset)? as usize;
        let directories = opt.at(
            count_offset + 4,
            count
                .checked_mul(8)
                .ok_or_else(|| invalid("invalid directory count"))?,
        )?;
        let headers = opt.dword(60)?;
        let image_size = opt.dword(56)?;
        let section_count = usize::from(coff.word(2)?);
        let section_offset = pe + 24 + optional_size;
        if headers > image_size || section_offset + section_count * 40 > headers as usize {
            return Err(invalid("invalid PE header extent"));
        }
        file.at(0, headers as usize)?;
        let table = file.at(section_offset, section_count * 40)?;
        let mut sections = Vec::new();
        for offset in (0..table.0.len()).step_by(40) {
            let section = Section {
                virtual_size: table.dword(offset + 8)?,
                rva: table.dword(offset + 12)?,
                raw_size: table.dword(offset + 16)?,
                raw: table.dword(offset + 20)?,
            };
            if section.raw_size != 0 {
                file.at(section.raw as usize, section.raw_size as usize)?;
            }
            let size = section.virtual_size.max(section.raw_size);
            if size != 0
                && (section.rva < headers
                    || u64::from(section.rva) + u64::from(size) > u64::from(image_size))
            {
                return Err(invalid("section outside PE image"));
            }
            sections.push(section);
        }
        sections.sort_by_key(|section| section.rva);
        let mut end = headers;
        for section in &sections {
            let size = section.virtual_size.max(section.raw_size);
            if size != 0 {
                if section.rva < end {
                    return Err(invalid("overlapping PE sections"));
                }
                end = section.rva + size;
            }
        }
        Ok(Self {
            file,
            sections,
            directories,
            arch,
            is_dll: coff.word(18)? & 0x2000 != 0,
            entry_rva: opt.dword(16)?,
            image_base: base,
            image_size,
            headers,
        })
    }

    fn rva(&self, address: u32) -> Result<Bytes<'a>> {
        if address < self.headers {
            return self
                .file
                .at(address as usize, (self.headers - address) as usize);
        }
        for section in &self.sections {
            if let Some(offset) = address.checked_sub(section.rva) {
                if offset < section.raw_size {
                    return self.file.at(
                        section.raw as usize + offset as usize,
                        (section.raw_size - offset) as usize,
                    );
                }
            }
        }
        Err(invalid("RVA has no file-backed bytes"))
    }

    fn name(&self, address: u32, prefix: usize) -> Result<String> {
        let bytes = self.rva(address)?;
        let tail = bytes
            .0
            .get(prefix..)
            .ok_or_else(|| invalid("invalid name RVA"))?;
        let end = tail
            .iter()
            .position(|&byte| byte == 0)
            .ok_or_else(|| invalid("unterminated PE import name"))?;
        let name =
            std::str::from_utf8(&tail[..end]).map_err(|_| invalid("import name is not UTF-8"))?;
        if name.is_empty() {
            return Err(invalid("empty import name"));
        }
        Ok(name.to_owned())
    }

    pub fn imports(&self) -> Result<Vec<Import>> {
        let mut imports = Vec::new();
        for (index, width, delayed) in [(1, 20, false), (13, 32, true)] {
            if index * 8 >= self.directories.0.len() {
                continue;
            }
            let address = self.directories.dword(index * 8)?;
            let size = self.directories.dword(index * 8 + 4)? as usize;
            if address == 0 || size == 0 {
                continue;
            }
            let directory = self.rva(address)?.at(0, size)?;
            let mut terminated = false;
            for raw in directory.0.chunks_exact(width) {
                if raw.iter().all(|&byte| byte == 0) {
                    terminated = true;
                    break;
                }
                let row = Bytes(raw);
                let (name, mut table, use_va) = if delayed {
                    let lookup = row.dword(16)?;
                    let iat = row.dword(12)?;
                    (
                        row.dword(4)?,
                        if lookup == 0 { iat } else { lookup },
                        row.dword(0)? & 1 == 0,
                    )
                } else {
                    let lookup = row.dword(0)?;
                    let iat = row.dword(16)?;
                    (
                        row.dword(12)?,
                        if lookup == 0 { iat } else { lookup },
                        false,
                    )
                };
                if name == 0 || table == 0 {
                    return Err(invalid("import descriptor is missing name or thunk table"));
                }
                let to_rva = |value: u64| {
                    let base = if use_va { self.image_base } else { 0 };
                    value
                        .checked_sub(base)
                        .and_then(|rva| u32::try_from(rva).ok())
                        .ok_or_else(|| invalid("invalid import address"))
                };
                table = to_rva(u64::from(table))?;
                let dll = self.name(to_rva(u64::from(name))?, 0)?.to_ascii_lowercase();
                let mut symbols = Vec::new();
                let pointer = self.arch.pointer_size();
                let mask = 1_u64 << (pointer * 8 - 1);
                let mut thunk_terminated = false;
                for thunk in self.rva(table)?.0.chunks_exact(pointer) {
                    let value = if pointer == 8 {
                        Bytes(thunk).qword(0)?
                    } else {
                        u64::from(Bytes(thunk).dword(0)?)
                    };
                    if value == 0 {
                        thunk_terminated = true;
                        break;
                    }
                    symbols.push(if value & mask != 0 {
                        Symbol::Ordinal(value as u16)
                    } else {
                        Symbol::Name(self.name(to_rva(value)?, 2)?)
                    });
                }
                if !thunk_terminated {
                    return Err(invalid("unterminated import thunk table"));
                }
                imports.push(Import {
                    dll,
                    symbols,
                    delayed,
                });
            }
            if !terminated {
                return Err(invalid("unterminated import descriptor table"));
            }
        }
        Ok(imports)
    }
}
