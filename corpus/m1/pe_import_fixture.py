"""Populate the import directory of the linked corpus executable."""

from pathlib import Path
import struct
import sys

template, executable, flat = map(Path, sys.argv[1:])
data = bytearray(template.read_bytes())
pe = struct.unpack_from("<I", data, 0x3C)[0]
section_count = struct.unpack_from("<H", data, pe + 6)[0]
optional_size = struct.unpack_from("<H", data, pe + 20)[0]
optional = pe + 24
image_size, header_size = struct.unpack_from("<II", data, optional + 56)
sections = {}
for i in range(section_count):
    offset = optional + optional_size + 40 * i
    name, virtual_size, rva, raw_size, raw = struct.unpack_from("<8sIIII", data, offset)
    sections[name.rstrip(b"\0")] = (virtual_size, rva, raw_size, raw)

size, rva, _, raw = sections[b".data"]
assert size >= 256
iat, directory, lookup, dll, name = (0, 32, 80, 112, 144)
entries = (rva + name, (1 << 63) | 17, 0)
for offset in (iat, lookup):
    struct.pack_into("<QQQ", data, raw + offset, *entries)
struct.pack_into("<IIIII", data, raw + directory, rva + lookup, 0, 0, rva + dll, rva + iat)
dll_name = b"Fixture.DLL\0"
data[raw + dll:raw + dll + len(dll_name)] = dll_name
hint_and_name = b"\0\0MixFour\0"
data[raw + name:raw + name + len(hint_and_name)] = hint_and_name
struct.pack_into("<II", data, optional + 112 + 8, rva + directory, 40)
struct.pack_into("<II", data, optional + 112 + 12 * 8, rva + iat, 24)
executable.write_bytes(data)

# The x86 oracle maps the exact same file-backed section bytes at their RVAs.
image = bytearray(image_size)
image[:header_size] = data[:header_size]
for _, section_rva, count, offset in sections.values():
    if count:
        image[section_rva:section_rva + count] = data[offset:offset + count]
flat.write_bytes(image)
