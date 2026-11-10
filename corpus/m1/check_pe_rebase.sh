#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?missing evidence directory}
mkdir -p "$evidence"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
lift=target/release/pf-ir-spike
guest="$evidence/rebase.exe"
if [[ $(uname -m) == x86_64 ]]; then
    as --64 corpus/m1/pe-rebase.s -o "$tmp/rebase.o"
    ld -mi386pep --entry pf_fixture --subsystem console --image-base 0x140000000 \
        --enable-reloc-section --no-insert-timestamp "$tmp/rebase.o" -o "$guest"
else
    reference=${2:?missing x86 evidence}
    cp "$reference/rebase.exe" "$guest"
fi
for base in 0x140000000 0x120000000 0x180000000; do
    work="$evidence/$base"
    mkdir -p "$work"
    "$lift" --pe "$guest" --image-base "$base" > "$work/lifted.c"
    "$lift" --pe-image "$guest" --image-base "$base" > "$work/image.bin"
    "$lift" --pe "$guest" --image-base "$base" > "$tmp/lifted.c"
    "$lift" --pe-image "$guest" --image-base "$base" > "$tmp/image.bin"
    cmp "$work/lifted.c" "$tmp/lifted.c"
    cmp "$work/image.bin" "$tmp/image.bin"
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$work" corpus/m1/pe-rebase.c -o "$tmp/check"
    "$tmp/check" "$work/image.bin" | tee "$work/result.txt"
    printf '%016x 0123456789abcdef\n' "$base" | cmp - "$work/result.txt"
    if [[ $(uname -m) != x86_64 ]]; then
        for file in lifted.c image.bin result.txt; do cmp "$work/$file" "$reference/$base/$file"; done
    fi
done
for mode in --pe --pe-image; do
    for bad in 0x0 0x10001 0x800000000000 0xffff7fff00000000; do
        if "$lift" "$mode" "$guest" --image-base "$bad" > "$tmp/bad.out" 2> "$tmp/bad.err"; then exit 1; fi
        test ! -s "$tmp/bad.out"
    done
done
