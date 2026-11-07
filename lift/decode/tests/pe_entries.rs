use pf_decode::{DecodeError, Mnemonic, PeDecodeError, PeDecoder};
use pf_detect::pe::Pe;
use std::{env, fs, path::PathBuf};

#[test]
#[ignore = "run through corpus/check_pe_decoder.py with the locked distlib wheel"]
fn decodes_real_pe32_and_pe32_plus_entry_instructions() {
    let fixtures = PathBuf::from(env::var_os("PORTFORGE_PE_FIXTURES").unwrap());
    // GNU objdump -d -w at AddressOfEntryPoint, for the pinned distlib 0.4.0 wheel.
    for (file, entry, expected) in [
        (
            "t32.exe",
            0x3be9,
            [
                (Mnemonic::CALL, 5, Some(0x409dcb)),
                (Mnemonic::JMP, 5, Some(0x403a88)),
                (Mnemonic::MOV, 2, None),
                (Mnemonic::PUSH, 1, None),
            ],
        ),
        (
            "t64.exe",
            0x427c,
            [
                (Mnemonic::SUB, 4, None),
                (Mnemonic::CALL, 5, Some(0x14000af10)),
                (Mnemonic::ADD, 4, None),
                (Mnemonic::JMP, 5, Some(0x140004104)),
            ],
        ),
    ] {
        let mut data = fs::read(fixtures.join(file)).unwrap();
        let image = Pe::parse(&data).unwrap();
        assert_eq!(image.entry_rva, entry);
        let decoder = PeDecoder::new(&image);
        let mut address = image.entry_rva;
        for (mnemonic, length, target) in expected {
            let instruction = decoder.decode_at(address).unwrap();
            assert_eq!(
                (instruction.mnemonic, instruction.length),
                (mnemonic, length)
            );
            if let Some(target) = target {
                let pc = image.image_base + u64::from(address);
                assert_eq!(
                    instruction.calc_absolute_address(pc, &instruction.visible_operands()[0]),
                    Ok(target)
                );
            }
            address += u32::from(length);
        }
        assert!(matches!(
            decoder.decode_at(0),
            Err(PeDecodeError::NoExecutableBytes { rva: 0 })
        ));

        // Trim .text after the first byte of the entry instruction. The remaining
        // bytes still exist on disk, but now lie outside the declared code extent.
        let pe_offset = u32::from_le_bytes(data[0x3c..0x40].try_into().unwrap()) as usize;
        let optional_size =
            u16::from_le_bytes(data[pe_offset + 20..pe_offset + 22].try_into().unwrap()) as usize;
        let section = pe_offset + 24 + optional_size;
        assert_eq!(&data[section..section + 8], b".text\0\0\0");
        let code_rva = u32::from_le_bytes(data[section + 12..section + 16].try_into().unwrap());
        data[section + 8..section + 12].copy_from_slice(&(entry - code_rva + 1).to_le_bytes());
        let image = Pe::parse(&data).unwrap();
        let decoder = PeDecoder::new(&image);
        assert_eq!(
            decoder.decode_at(entry),
            Err(PeDecodeError::Instruction {
                rva: entry,
                error: DecodeError::Truncated
            })
        );
    }
}
