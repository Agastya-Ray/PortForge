"""Instruction bytes shared by emitted-C and independent real-x86 execution."""
from pathlib import Path
import json
import subprocess
import sys

forms = []

def add(name, code, width, memory=False, write=False, logic=False, noflags=False):
    forms.append((name, bytes(code).hex(), width, memory, write, logic, noflags))

def prefix(width):
    return [0x66] if width == 16 else ([0x48] if width == 64 else [])

for width in (8, 16, 32, 64):
    p = prefix(width)
    for name, base, group in (("add", 0x00, 0), ("adc", 0x10, 2), ("sbb", 0x18, 3),
                              ("sub", 0x28, 5), ("and", 0x20, 4), ("or", 0x08, 1),
                              ("xor", 0x30, 6), ("cmp", 0x38, 7), ("test", 0x84, 0)):
        writes = name not in ("cmp", "test")
        logic = name in ("and", "or", "xor", "test")
        op = base + (width != 8)
        if width <= 16:
            add(f"{name}{width} register", p + [op, 0xc8], width, logic=logic)
            add(f"{name}{width} memory source", p + [op if name == "test" else op + 2, 0x02], width, True, logic=logic)
        if width <= 16 or writes:
            add(f"{name}{width} memory destination", p + [op, 0x0a], width, True, writes, logic)
        imm = [0xff] * min(width // 8, 4)
        opcode = (0xf6 + (width != 8)) if name == "test" else (0x80 if width == 8 else 0x81)
        if width <= 16:
            add(f"{name}{width} immediate", p + [opcode, 0xc0 + 8 * group] + imm, width, logic=logic)
        if width <= 16 or writes:
            add(f"{name}{width} memory immediate", p + [opcode, 0x02 + 8 * group] + imm, width, True, writes, logic)
    for name, byte_op, group in (("inc", 0xfe, 0), ("dec", 0xfe, 1), ("neg", 0xf6, 3), ("not", 0xf6, 2)):
        opcode = byte_op + (width != 8)
        if width <= 16:
            add(f"{name}{width} register", p + [opcode, 0xc0 + 8 * group], width, noflags=name == "not")
        add(f"{name}{width} memory", p + [opcode, 0x02 + 8 * group], width, True, True, noflags=name == "not")
    if width <= 16:
        for suffix, code, memory, write in (
            ("register", [0x88 + (width != 8), 0xc8], False, False),
            ("load", [0x8a + (width != 8), 0x02], True, False),
            ("store", [0x88 + (width != 8), 0x0a], True, True),
            ("immediate", [0xb0 if width == 8 else 0xb8] + [0xa5] * (width // 8), False, False),
            ("memory immediate", [0xc6 + (width != 8), 0x02] + [0xa5] * (width // 8), True, True),
        ):
            add(f"mov{width} {suffix}", p + code, width, memory, write, noflags=True)
    if width != 8:
        for condition in range(16):
            add(f"cmov{condition} {width} memory", p + [0x0f, 0x40 + condition, 0x02], width, True, noflags=True)
            if width == 16:
                add(f"cmov{condition} 16 register", p + [0x0f, 0x40 + condition, 0xc1], width, noflags=True)

for name, base in (("add", 0x00), ("adc", 0x10), ("sbb", 0x18), ("sub", 0x28),
                   ("and", 0x20), ("or", 0x08), ("xor", 0x30), ("cmp", 0x38), ("test", 0x84), ("mov", 0x88)):
    add(f"{name} AH CH", [base, 0xec], 8, logic=name in ("and", "or", "xor", "test"), noflags=name == "mov")
add("not AH", [0xf6, 0xd4], 8, noflags=True)
add("neg AH", [0xf6, 0xdc], 8)


def main():
    work = Path(sys.argv[1])
    code = subprocess.check_output([sys.argv[2], *(f[1] for f in forms)])
    repeated = subprocess.check_output([sys.argv[2], *(f[1] for f in forms)])
    assert code == repeated, "non-deterministic scalar C emission"
    (work / "lifted.c").write_bytes(code)
    lines = ["static const struct test_case cases[] = {"]
    for index, (name, _, width, memory, write, logic, noflags) in enumerate(forms):
        lines.append(f"{{pf_block_{index}, {json.dumps(name)}, {width}, {int(memory)}, {int(write)}, {int(logic)}, {int(noflags)}}},")
    lines += ["};", "#if defined(__x86_64__)", "static void native(unsigned test, uint64_t *a, uint64_t *b, uint8_t *bytes, uint64_t *flags) {", "switch(test) {"]
    for index, (_, code, *_) in enumerate(forms):
        raw = ",".join("0x" + code[i:i+2] for i in range(0, len(code), 2))
        lines.append(f'case {index}: NATIVE_BYTES("{raw}"); break;')
    lines += ["default: abort();", "}", "}", "#endif"]
    (work / "scalar-storage-cases.h").write_text("\n".join(lines) + "\n")
    print(f"scalar-storage: {len(forms)} forms, 1000000 cases per form", file=sys.stderr)

if __name__ == "__main__":
    main()
