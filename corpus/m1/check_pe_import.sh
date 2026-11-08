#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_import.sh <evidence> [reference-evidence]}
mkdir -p "$evidence"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
lift=target/release/pf-ir-spike
args=()
if [[ $(uname -m) == x86_64 ]]; then
    as --64 corpus/m1/pe-import.s -o "$tmp/import.o"
    for kind in first second; do
        ld -mi386pep --entry pf_fixture --subsystem console --image-base 0x140000000 \
            --no-insert-timestamp "$tmp/import.o" -o "$tmp/$kind.template.exe"
        python3 corpus/m1/pe_import_fixture.py "$tmp/$kind.template.exe" "$tmp/$kind.exe" "$tmp/$kind.bin"
    done
    cmp "$tmp/first.exe" "$tmp/second.exe"
    cp "$tmp/first.exe" "$evidence/import.exe"
    args+=("$tmp/first.bin")
    if "$lift" --pe "$tmp/first.template.exe" > "$tmp/bad.c" 2> "$tmp/bad.log"; then exit 1; fi
    test ! -s "$tmp/bad.c"
    grep -q 'is not an eager import' "$tmp/bad.log"
else
    reference=${2:?missing x86 reference evidence}
    cp "$reference/import.exe" "$evidence/import.exe"
fi

"$lift" --pe "$evidence/import.exe" > "$evidence/lifted.c"
"$lift" --pe "$evidence/import.exe" > "$tmp/repeated.c"
cmp "$evidence/lifted.c" "$tmp/repeated.c"
for executable in "$tmp/check" "$tmp/repeated"; do
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$evidence" corpus/m1/pe-import.c -o "$executable"
done
cmp "$tmp/check" "$tmp/repeated"
"$tmp/check" "${args[@]}" | tee "$evidence/digest.txt"
if [[ $(uname -m) != x86_64 ]]; then
    cmp "$evidence/lifted.c" "$reference/lifted.c"
    cmp "$evidence/digest.txt" "$reference/digest.txt"
fi
