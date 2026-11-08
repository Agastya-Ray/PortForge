#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_entry.sh <evidence> [reference-evidence] [scalar]}
options=()
case ${3:-entry} in
    entry) ;;
    scalar) options=(-DPF_SCALAR_FIXTURE) ;;
    *) exit 2 ;;
esac
mkdir -p "$evidence"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
lift=target/release/pf-ir-spike
args=()
if [[ $(uname -m) == x86_64 ]]; then
    if [[ ${3:-entry} == scalar ]]; then
        cc -O2 -fno-ident -fcf-protection=none -fno-asynchronous-unwind-tables \
            -fno-stack-protector -c corpus/m1/pe-scalar-source.c -o "$tmp/entry.o"
    else
        as --64 corpus/m1/pe-entry.s -o "$tmp/entry.o"
    fi
    for pe in "$evidence/entry.exe" "$tmp/repeated.exe"; do
        ld -mi386pep --entry pf_fixture --subsystem console --image-base 0x140000000 \
            --no-insert-timestamp "$tmp/entry.o" -o "$pe"
    done
    cmp "$evidence/entry.exe" "$tmp/repeated.exe"
    objcopy -O binary -j .text "$evidence/entry.exe" "$tmp/entry.bin"
    args+=("$tmp/entry.bin")
else
    reference=${2:?missing x86 reference evidence}
    cp "$reference/entry.exe" "$evidence/entry.exe"
fi

"$lift" --pe "$evidence/entry.exe" > "$evidence/lifted.c"
"$lift" --pe "$evidence/entry.exe" > "$tmp/repeated.c"
cmp "$evidence/lifted.c" "$tmp/repeated.c"
for executable in "$tmp/check" "$tmp/repeated"; do
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "${options[@]}" -I"$evidence" corpus/m1/pe-entry.c -o "$executable"
done
cmp "$tmp/check" "$tmp/repeated"
"$tmp/check" "${args[@]}" | tee "$evidence/digest.txt"
if [[ $(uname -m) != x86_64 ]]; then
    cmp "$evidence/lifted.c" "$reference/lifted.c"
    cmp "$evidence/digest.txt" "$reference/digest.txt"
fi

python3 - "$lift" "$evidence/entry.exe" "$tmp" <<'PY'
from pathlib import Path
import struct
import subprocess
import sys

lift, fixture, directory = sys.argv[1:]
binary = Path(fixture).read_bytes()
pe = struct.unpack_from("<I", binary, 0x3c)[0]
entry = struct.unpack_from("<I", binary, pe + 24 + 16)[0]
size = struct.unpack_from("<H", binary, pe + 20)[0]
first_section = pe + 24 + size
_, code, _, raw = struct.unpack_from("<IIII", binary, first_section + 8)
assert binary[first_section:first_section + 5] == b".text"
assert entry == code, "the native oracle starts at the beginning of .text"
entry_offset = raw + entry - code
for replacement, diagnostic in [
    (b"\xff\xe0", "nonrelative JMP"),
    (b"\xe9\x00\x00\x00\x20", "no file-backed executable bytes"),
    (b"\xeb\xff", "overlaps a decoded instruction"),
]:
    bad = Path(directory) / "bad.exe"
    bad.write_bytes(binary[:entry_offset] + replacement + binary[entry_offset + len(replacement):])
    result = subprocess.run([lift, "--pe", str(bad)], capture_output=True, text=True)
    assert result.returncode != 0 and not result.stdout, result
    assert diagnostic in result.stderr and "RVA" in result.stderr, result.stderr
PY
