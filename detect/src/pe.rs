use std::io::{Error, ErrorKind, Result};

const IMAGE_SCN_MEM_EXECUTE: u32 = 0x2000_0000;

fn invalid(message: &str) -> Error {
    Error::new(ErrorKind::InvalidData, message)
}

fn advance_rva(address: u32, size: usize) -> Result<u32> {
    u32::try_from(size)
        .ok()
        .and_then(|size| address.checked_add(size))
        .ok_or_else(|| invalid("RVA range overflows"))
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
    pub eager_iat_rva: Option<u32>,
}

#[derive(Clone, Copy)]
struct Section {
    rva: u32,
    virtual_size: u32,
    raw: u32,
    raw_size: u32,
    characteristics: u32,
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
                characteristics: table.dword(offset + 36)?,
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

    /// Read a bounded prefix of executable, file-backed bytes starting at an RVA.
    /// Stop at the first gap, non-executable section or virtual-only byte.
    pub fn read_executable(&self, mut rva: u32, output: &mut [u8]) -> usize {
        let mut copied = 0;
        while copied < output.len() {
            let region = self.sections.iter().find_map(|section| {
                if section.characteristics & IMAGE_SCN_MEM_EXECUTE == 0 {
                    return None;
                }
                let offset = rva.checked_sub(section.rva)?;
                let file_size = if section.virtual_size == 0 {
                    section.raw_size
                } else {
                    section.raw_size.min(section.virtual_size)
                };
                if offset >= file_size {
                    return None;
                }
                let start = section.raw as usize + offset as usize;
                let end = start + (file_size - offset) as usize;
                Some(&self.file.0[start..end])
            });
            let Some(region) = region else {
                break;
            };
            let count = region.len().min(output.len() - copied);
            output[copied..copied + count].copy_from_slice(&region[..count]);
            copied += count;
            // Section validation bounds this increment by SizeOfImage.
            rva += count as u32;
        }
        copied
    }

    fn rva_region(&self, address: u32) -> Result<Bytes<'a>> {
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

    fn read_rva(&self, mut address: u32, mut output: &mut [u8]) -> Result<()> {
        while !output.is_empty() {
            let region = self.rva_region(address)?.0;
            let count = region.len().min(output.len());
            output[..count].copy_from_slice(&region[..count]);
            output = &mut output[count..];
            if !output.is_empty() {
                address = advance_rva(address, count)?;
            }
        }
        Ok(())
    }

    fn name(&self, address: u32, has_hint: bool) -> Result<String> {
        let mut address = address;
        if has_hint {
            self.read_rva(address, &mut [0; 2])?;
            address = advance_rva(address, 2)?;
        }
        let mut name = Vec::new();
        loop {
            let tail = self
                .rva_region(address)
                .map_err(|_| invalid("unterminated PE import name"))?
                .0;
            if let Some(end) = tail.iter().position(|&byte| byte == 0) {
                name.extend_from_slice(&tail[..end]);
                break;
            }
            name.extend_from_slice(tail);
            address = advance_rva(address, tail.len())?;
        }
        if name.is_empty() {
            return Err(invalid("empty import name"));
        }
        String::from_utf8(name).map_err(|_| invalid("import name is not UTF-8"))
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
            if advance_rva(address, size)? > self.image_size {
                return Err(invalid("import directory outside image"));
            }
            let mut terminated = false;
            let mut raw = [0; 32];
            let raw = &mut raw[..width];
            for index in 0..size / width {
                self.read_rva(advance_rva(address, index * width)?, raw)?;
                if raw.iter().all(|&byte| byte == 0) {
                    terminated = true;
                    break;
                }
                let row = Bytes(raw);
                let (name, mut table, iat, use_va) = if delayed {
                    let lookup = row.dword(16)?;
                    let iat = row.dword(12)?;
                    (
                        row.dword(4)?,
                        if lookup == 0 { iat } else { lookup },
                        iat,
                        row.dword(0)? & 1 == 0,
                    )
                } else {
                    let lookup = row.dword(0)?;
                    let iat = row.dword(16)?;
                    (
                        row.dword(12)?,
                        if lookup == 0 { iat } else { lookup },
                        iat,
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
                let dll = self
                    .name(to_rva(u64::from(name))?, false)?
                    .to_ascii_lowercase();
                let mut symbols = Vec::new();
                let pointer = self.arch.pointer_size();
                let mask = 1_u64 << (pointer * 8 - 1);
                loop {
                    let mut thunk = [0; 8];
                    self.read_rva(table, &mut thunk[..pointer])
                        .map_err(|_| invalid("unterminated import thunk table"))?;
                    let value = u64::from_le_bytes(thunk);
                    if value == 0 {
                        break;
                    }
                    symbols.push(if value & mask != 0 {
                        Symbol::Ordinal(value as u16)
                    } else {
                        Symbol::Name(self.name(to_rva(value)?, true)?)
                    });
                    table = advance_rva(table, pointer)?;
                }
                imports.push(Import {
                    dll,
                    symbols,
                    delayed,
                    eager_iat_rva: (!delayed && iat != 0).then_some(iat),
                });
            }
            if !terminated {
                return Err(invalid("unterminated import descriptor table"));
            }
        }
        Ok(imports)
    }

    /// Validate the file-backed eager IAT before binding it for execution.
    pub fn eager_iat(&self, import: &Import) -> Result<Option<u32>> {
        let Some(first) = import.eager_iat_rva else {
            return Ok(None);
        };
        let width = self.arch.pointer_size();
        let size = import
            .symbols
            .len()
            .checked_add(1)
            .and_then(|count| count.checked_mul(width))
            .ok_or_else(|| invalid("IAT size overflows"))?;
        let end = advance_rva(first, size)?;
        if first < self.headers || end > self.image_size {
            return Err(invalid("IAT range is outside image sections"));
        }
        let mut cursor = first;
        while cursor < end {
            let region = self
                .rva_region(cursor)
                .map_err(|_| invalid("IAT range is not file-backed"))?;
            cursor += region.0.len().min((end - cursor) as usize) as u32;
        }
        let mut terminator = [0; 8];
        self.read_rva(end - width as u32, &mut terminator[..width])?;
        if terminator != [0; 8] {
            return Err(invalid("IAT is missing its terminator"));
        }
        Ok(Some(first))
    }
}

impl Pe<'_> {
    /// Materialize the guest image as bytes. The caller supplies its memory budget.
    pub fn map_image(&self, base: u64, budget: usize) -> Result<Vec<u8>> {
        let size = self.image_size as usize;
        let end = base
            .checked_add(u64::from(self.image_size))
            .ok_or_else(|| invalid("image range overflows guest address space"))?;
        if matches!(self.arch, Architecture::X86) && end > (1_u64 << 32) {
            return Err(invalid("PE32 image does not fit in guest address space"));
        }
        if size > budget {
            return Err(invalid("PE image exceeds the mapping budget"));
        }
        let mut image = Vec::new();
        image.try_reserve_exact(size).map_err(Error::other)?;
        image.resize(size, 0);
        image[..self.headers as usize].copy_from_slice(self.file.at(0, self.headers as usize)?.0);
        for section in &self.sections {
            if section.raw_size != 0 {
                let start = section.rva as usize;
                let length = section.raw_size as usize;
                image[start..start + length]
                    .copy_from_slice(self.file.at(section.raw as usize, length)?.0);
            }
        }
        if base != self.image_base {
            self.relocate(base.wrapping_sub(self.image_base), &mut image)?;
        }
        Ok(image)
    }

    fn relocate(&self, delta: u64, image: &mut [u8]) -> Result<()> {
        let directory = self
            .directories
            .at(5 * 8, 8)
            .map_err(|_| invalid("image has no base relocation directory"))?;
        let address = directory.dword(0)?;
        let size = directory.dword(4)? as usize;
        if address == 0 || size == 0 {
            return Err(invalid("image has no base relocations"));
        }
        if advance_rva(address, size)? > self.image_size {
            return Err(invalid("base relocation directory outside image"));
        }
        let mut cursor = 0;
        while cursor < size {
            let mut header = [0; 8];
            if size - cursor < header.len() {
                return Err(invalid("truncated base relocation header"));
            }
            self.read_rva(advance_rva(address, cursor)?, &mut header)?;
            let header = Bytes(&header);
            let page = header.dword(0)? as usize;
            let block_size = header.dword(4)? as usize;
            if block_size < 8
                || block_size % 2 != 0
                || block_size > size - cursor
                || page % 4096 != 0
            {
                return Err(invalid("invalid base relocation block"));
            }
            let end = cursor + block_size;
            cursor += 8;
            let mut next_entry = || -> Result<Option<u16>> {
                if cursor == end {
                    return Ok(None);
                }
                let mut entry = [0; 2];
                self.read_rva(advance_rva(address, cursor)?, &mut entry)?;
                cursor += 2;
                Ok(Some(u16::from_le_bytes(entry)))
            };
            while let Some(entry) = next_entry()? {
                let kind = entry >> 12;
                let offset = page + usize::from(entry & 0xfff);
                let width = match (self.arch, kind) {
                    (_, 0) => continue,
                    (Architecture::X86, 1 | 2 | 4) => 2,
                    (Architecture::X86, 3) => 4,
                    (Architecture::X64, 10) => 8,
                    _ => return Err(invalid("unsupported base relocation type")),
                };
                let field = image
                    .get_mut(offset..offset + width)
                    .ok_or_else(|| invalid("base relocation outside image"))?;
                let raw = Bytes(field);
                match kind {
                    1 | 2 => {
                        let adjustment = if kind == 1 { delta >> 16 } else { delta };
                        let value = raw.word(0)?.wrapping_add(adjustment as u16);
                        field.copy_from_slice(&value.to_le_bytes());
                    }
                    3 => {
                        let value = raw.dword(0)?.wrapping_add(delta as u32);
                        field.copy_from_slice(&value.to_le_bytes());
                    }
                    4 => {
                        let next =
                            next_entry()?.ok_or_else(|| invalid("missing HIGHADJ addend"))?;
                        let low = i32::from(next as i16) as u32;
                        let value = (u32::from(raw.word(0)?) << 16)
                            .wrapping_add(low)
                            .wrapping_add(delta as u32)
                            .wrapping_add(0x8000);
                        field.copy_from_slice(&((value >> 16) as u16).to_le_bytes());
                    }
                    10 => {
                        let value = raw.qword(0)?.wrapping_add(delta);
                        field.copy_from_slice(&value.to_le_bytes());
                    }
                    _ => unreachable!("relocation type already checked"),
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(arch: Architecture) -> Vec<u8> {
        let mut data = vec![0; 0x400];
        let (machine, magic, base, count_offset, optional_size, relocation) = match arch {
            Architecture::X86 => (0x14c_u16, 0x10b_u16, 0x400000_u64, 92, 224_u16, 0x3010_u16),
            Architecture::X64 => (0x8664, 0x20b, 0x140000000, 108, 240, 0xa010),
        };
        let optional = 0x98;
        let section = optional + usize::from(optional_size);
        data[0..2].copy_from_slice(b"MZ");
        data[0x3c..0x40].copy_from_slice(&0x80_u32.to_le_bytes());
        data[0x80..0x84].copy_from_slice(b"PE\0\0");
        data[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
        data[0x86..0x88].copy_from_slice(&1_u16.to_le_bytes());
        data[0x94..0x96].copy_from_slice(&optional_size.to_le_bytes());
        data[optional..optional + 2].copy_from_slice(&magic.to_le_bytes());
        match arch {
            Architecture::X86 => {
                data[optional + 28..optional + 32].copy_from_slice(&(base as u32).to_le_bytes())
            }
            Architecture::X64 => {
                data[optional + 24..optional + 32].copy_from_slice(&base.to_le_bytes())
            }
        }
        for (offset, value) in [
            (optional + 56, 0x2000_u32),
            (optional + 60, 0x200),
            (optional + count_offset, 16),
            (optional + count_offset + 44, 0x1020),
            (optional + count_offset + 48, 12),
            (section + 8, 0x300),
            (section + 12, 0x1000),
            (section + 16, 0x200),
            (section + 20, 0x200),
            (0x220, 0x1000),
            (0x224, 12),
        ] {
            data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        data[0x210..0x218].copy_from_slice(&(base + 0x50).to_le_bytes());
        data[0x228..0x22a].copy_from_slice(&relocation.to_le_bytes());
        data
    }

    fn add_adjacent_section(data: &mut Vec<u8>) {
        let first = 0x98 + 240;
        let second = first + 40;
        data.resize(0x800, 0);
        data[0x86..0x88].copy_from_slice(&2_u16.to_le_bytes());
        data[first + 8..first + 12].copy_from_slice(&0x200_u32.to_le_bytes());
        for (offset, value) in [(8, 0x200_u32), (12, 0x1200), (16, 0x200), (20, 0x600)] {
            data[second + offset..second + offset + 4].copy_from_slice(&value.to_le_bytes());
        }
    }

    fn write_rva(data: &mut [u8], address: u32, bytes: &[u8]) {
        for (index, &byte) in bytes.iter().enumerate() {
            let offset = address as usize + index - 0x1000;
            let file_offset = if offset < 0x200 {
                0x200 + offset
            } else {
                0x400 + offset
            };
            data[file_offset] = byte;
        }
    }

    #[test]
    fn executable_reads_follow_adjacent_regions_and_stop_at_data() {
        let mut data = fixture(Architecture::X64);
        add_adjacent_section(&mut data);
        let first = 0x98 + 240;
        let second = first + 40;
        data[first + 36..first + 40].copy_from_slice(&IMAGE_SCN_MEM_EXECUTE.to_le_bytes());
        write_rva(&mut data, 0x11ff, &[0x48, 0x01, 0xc8]);
        let mut window = [0; 3];

        let pe = Pe::parse(&data).unwrap();
        assert_eq!(pe.read_executable(0x11ff, &mut window), 1);
        assert_eq!(pe.read_executable(0x1200, &mut window), 0);
        assert_eq!(pe.read_executable(0, &mut window), 0);

        data[second + 36..second + 40].copy_from_slice(&IMAGE_SCN_MEM_EXECUTE.to_le_bytes());
        let pe = Pe::parse(&data).unwrap();
        assert_eq!(pe.read_executable(0x11ff, &mut window), 3);
        assert_eq!(window, [0x48, 0x01, 0xc8]);
        assert_eq!(pe.read_executable(0x1400, &mut window), 0);
    }

    #[test]
    fn executable_reads_skip_file_padding_and_virtual_tails() {
        let mut data = fixture(Architecture::X64);
        let section = 0x98 + 240;
        data[section + 36..section + 40].copy_from_slice(&IMAGE_SCN_MEM_EXECUTE.to_le_bytes());
        let mut window = [0; 4];

        let pe = Pe::parse(&data).unwrap();
        assert_eq!(pe.read_executable(0x11ff, &mut window), 1);
        assert_eq!(pe.read_executable(0x1200, &mut window), 0);

        data[section + 8..section + 12].copy_from_slice(&0x100_u32.to_le_bytes());
        let pe = Pe::parse(&data).unwrap();
        assert_eq!(pe.read_executable(0x10ff, &mut window), 1);
        assert_eq!(pe.read_executable(0x1100, &mut window), 0);

        data[section + 8..section + 12].fill(0);
        let pe = Pe::parse(&data).unwrap();
        assert_eq!(pe.read_executable(0x11ff, &mut window), 1);
    }

    #[test]
    fn imports_cross_adjacent_rva_regions() {
        for (descriptor, name, thunk, symbol) in [
            (0x11f8_u32, 0x1100_u32, 0x1120_u32, 0x1140_u32),
            (0x1040, 0x11fa, 0x1120, 0x1140),
            (0x1040, 0x1100, 0x11f8, 0x1140),
            (0x1040, 0x1100, 0x1120, 0x11fd),
        ] {
            let mut data = fixture(Architecture::X64);
            add_adjacent_section(&mut data);
            let directory = 0x98 + 112 + 8;
            data[directory..directory + 4].copy_from_slice(&descriptor.to_le_bytes());
            data[directory + 4..directory + 8].copy_from_slice(&40_u32.to_le_bytes());
            write_rva(&mut data, descriptor, &thunk.to_le_bytes());
            write_rva(&mut data, descriptor + 12, &name.to_le_bytes());
            write_rva(&mut data, descriptor + 16, &0x1170_u32.to_le_bytes());
            write_rva(&mut data, name, b"kernel32.dll\0");
            write_rva(&mut data, thunk, &u64::from(symbol).to_le_bytes());
            write_rva(&mut data, thunk + 8, &((1_u64 << 63) | 19).to_le_bytes());
            write_rva(&mut data, symbol, b"\0\0GetLastError\0");
            let pe = Pe::parse(&data).unwrap();
            let mut imports = pe.imports().unwrap();
            assert_eq!(imports.len(), 1);
            assert_eq!(imports[0].dll, "kernel32.dll");
            assert_eq!(imports[0].eager_iat_rva, Some(0x1170));
            assert!(
                matches!(&imports[0].symbols[..], [Symbol::Name(name), Symbol::Ordinal(19)]
                if name == "GetLastError")
            );
            assert_eq!(pe.eager_iat(&imports[0]).unwrap(), Some(0x1170));
            imports[0].eager_iat_rva = Some(0x800);
            assert!(pe.eager_iat(&imports[0]).is_err());
            imports[0].eager_iat_rva = Some(pe.image_size - 8);
            assert!(pe.eager_iat(&imports[0]).is_err());
            imports[0].eager_iat_rva = Some(0x1170);
            write_rva(&mut data, 0x1180, &[1]);
            assert!(Pe::parse(&data).unwrap().eager_iat(&imports[0]).is_err());
        }
    }

    #[test]
    fn relocations_cross_adjacent_rva_regions_and_reject_a_gap() {
        let mut data = fixture(Architecture::X64);
        add_adjacent_section(&mut data);
        let directory = 0x98 + 112 + 5 * 8;
        data[directory..directory + 4].copy_from_slice(&0x11fc_u32.to_le_bytes());
        let block: [u8; 12] = data[0x220..0x22c].try_into().unwrap();
        write_rva(&mut data, 0x11fc, &block);
        let pe = Pe::parse(&data).unwrap();
        let base = pe.image_base + 0x10000;
        let rebased = pe.map_image(base, 0x2000).unwrap();
        assert_eq!(Bytes(&rebased).qword(0x1010).unwrap(), base + 0x50);

        let second = 0x98 + 240 + 40;
        data[second + 12..second + 16].copy_from_slice(&0x1300_u32.to_le_bytes());
        let pe = Pe::parse(&data).unwrap();
        assert!(pe.map_image(base, 0x2000).is_err());
    }

    #[test]
    fn maps_and_rebases_both_guest_architectures() {
        for arch in [Architecture::X86, Architecture::X64] {
            let data = fixture(arch);
            let pe = Pe::parse(&data).unwrap();
            let preferred = pe.map_image(pe.image_base, 0x2000).unwrap();
            assert_eq!(&preferred[..0x200], &data[..0x200]);
            assert_eq!(&preferred[0x1000..0x1200], &data[0x200..]);
            assert!(preferred[0x1200..0x1300].iter().all(|&byte| byte == 0));
            let base = pe.image_base - 0x10000;
            let rebased = pe.map_image(base, 0x2000).unwrap();
            assert_eq!(Bytes(&rebased).qword(0x1010).unwrap(), base + 0x50);
            assert!(pe.map_image(base, 0x1fff).is_err());
        }
    }

    #[test]
    fn rejects_out_of_range_relocation() {
        let mut data = fixture(Architecture::X64);
        data[0x220..0x224].copy_from_slice(&0x3000_u32.to_le_bytes());
        let pe = Pe::parse(&data).unwrap();
        assert!(pe.map_image(pe.image_base + 0x10000, 0x2000).is_err());
    }
}
