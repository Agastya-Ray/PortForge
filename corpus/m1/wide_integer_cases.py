"""Wide integer instruction fixtures and tiny PE dispatcher tests."""
from pathlib import Path
import struct
import subprocess
import sys

forms = []
for width in (8, 16, 32, 64):
    prefix = [0x66] if width == 16 else ([0x48] if width == 64 else [])
    opcode = 0xf6 if width == 8 else 0xf7
    for operation, group in enumerate((4, 5, 6, 7)):
        for name, modrm, register, shift in (("CX", 0xc1, 1, 0), ("memory", 0x07, -1, 0), ("AX", 0xc0, 0, 0), ("DX", 0xc2, 2, 0)):
            forms.append((f"{('mul', 'imul', 'div', 'idiv')[operation]}{width} {name}", prefix + [opcode, modrm + 8 * group], width, operation, register, shift))
        forms.append((f"{('mul', 'imul', 'div', 'idiv')[operation]}{width} R8", ([0x66, 0x41] if width == 16 else ([0x49] if width == 64 else [0x41])) + [opcode, 0xc0 + 8 * group], width, operation, 8, 0))
        if width == 8:
            forms.append((f"{('mul', 'imul', 'div', 'idiv')[operation]}8 AH", [opcode, 0xc4 + 8 * group], width, operation, 0, 8))
for name, code, source, target, high in (("cbw", [0x66, 0x98], 8, 16, 0), ("cwde", [0x98], 16, 32, 0), ("cdqe", [0x48, 0x98], 32, 64, 0), ("cwd", [0x66, 0x99], 16, 16, 1), ("cdq", [0x99], 32, 32, 1), ("cqo", [0x48, 0x99], 64, 64, 1)):
    forms.append((name, code, target, 4, source, high))


def pe(code):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<I', data, optional + 4, 512)
    struct.pack_into('<II', data, optional + 16, 0x1000, 0x1000)
    struct.pack_into('<Q', data, optional + 24, 0x140000000)
    struct.pack_into('<II', data, optional + 32, 0x1000, 512)
    struct.pack_into('<II', data, optional + 56, 0x2000, 512)
    struct.pack_into('<H', data, optional + 68, 3)
    struct.pack_into('<I', data, optional + 108, 16)
    struct.pack_into('<8sIIIIIIHHI', data, optional + 240, b'.text', len(code), 0x1000, 512, 512, 0, 0, 0, 0, 0x60000020)
    data[512:512+len(code)] = code
    return data


def main():
    work = Path(sys.argv[1])
    lift = sys.argv[2]
    blocks = [bytes(f[1]).hex() for f in forms]
    code = subprocess.check_output([lift, *blocks])
    assert code == subprocess.check_output([lift, *blocks])
    (work / 'lifted.c').write_bytes(code)
    lines = ['static const struct wide_case cases[] = {']
    import json
    for i, (name, raw, width, operation, register, shift) in enumerate(forms):
        lines.append(f'{{pf_block_{i}, {json.dumps(name)}, {width}, {operation}, {register}, {shift}, {len(raw)}}},')
    lines += ['};', '#if defined(__x86_64__)', 'static void native(unsigned test, struct pf_state *state, uint8_t *bytes, uint64_t *flags) {', 'register uint64_t extended __asm__("r8") = state->gpr[8];', 'switch (test) {']
    for i, (_, raw, *_) in enumerate(forms):
        literal = ','.join(f'0x{x:02x}' for x in raw)
        lines.append(f'case {i}: NATIVE_BYTES("{literal}"); break;')
    lines += ['default: abort();', '}', 'state->gpr[8] = extended;', '}', '#endif']
    (work / 'wide-integer-cases.h').write_text('\n'.join(lines) + '\n')
    for name, a, d, c in (('zero', 123, 0, 0), ('overflow', 0, 1, 1), ('valid', 107, 0, 10)):
        code = b'\x48\xb8' + struct.pack('<Q', a) + b'\x48\xba' + struct.pack('<Q', d) + b'\x48\xb9' + struct.pack('<Q', c) + b'\x48\xf7\xf1\xc3'
        folder = work / name
        folder.mkdir()
        executable = folder / 'guest.exe'
        executable.write_bytes(pe(code))
        (folder / 'lifted.c').write_bytes(subprocess.check_output([lift, '--pe', str(executable)]))
    print(f'wide-integers: {len(forms)} forms, 1000000 inputs per form', file=sys.stderr)

if __name__ == '__main__':
    main()
