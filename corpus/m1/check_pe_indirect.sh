#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_indirect.sh <evidence> [reference-evidence]}
mkdir -p "$evidence"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
lift=target/release/pf-ir-spike
guest="$evidence/indirect.exe"
args=()
if [[ $(uname -m) == x86_64 ]]; then
    as --64 corpus/m1/pe-indirect.s -o "$tmp/indirect.o"
    for exe in "$guest" "$tmp/repeated.exe"; do
        ld -mi386pep --entry pf_fixture --subsystem console --image-base 0x140000000 \
            --no-insert-timestamp "$tmp/indirect.o" -o "$exe"
    done
    cmp "$guest" "$tmp/repeated.exe"
    objcopy -O binary -j .text "$guest" "$tmp/indirect.bin"
    args+=("$tmp/indirect.bin")
else
    reference=${2:?missing x86 indirect-call evidence}
    cp "$reference/indirect.exe" "$guest"
fi

"$lift" --pe "$guest" --seed-rva 0x1080 --seed-rva 0x1090 > "$evidence/lifted.c"
"$lift" --pe "$guest" --seed-rva 0x1090 --seed-rva 0x1080 --seed-rva 0x1080 > "$tmp/repeated.c"
cmp "$evidence/lifted.c" "$tmp/repeated.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$evidence" corpus/m1/pe-indirect.c -o "$tmp/check"
"$tmp/check" "${args[@]}" | tee "$evidence/digest.txt"

if "$lift" --pe "$guest" --seed-rva 0x1080 --seed-rva 0x1081 > "$tmp/bad.c" 2> "$tmp/bad.log"; then exit 1; fi
test ! -s "$tmp/bad.c"
rg -q 'overlaps a decoded instruction' "$tmp/bad.log"
if "$lift" --pe "$guest" --seed-rva 0xfffffff0 > "$tmp/bad.c" 2> "$tmp/bad.log"; then exit 1; fi
test ! -s "$tmp/bad.c"
rg -q 'no file-backed executable bytes' "$tmp/bad.log"
if [[ $(uname -m) != x86_64 ]]; then
    cmp "$evidence/lifted.c" "$reference/lifted.c"
    cmp "$evidence/digest.txt" "$reference/digest.txt"
fi
