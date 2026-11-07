#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_arithmetic.sh <evidence-directory>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"

cargo build --release --locked --offline -p pf-ir-spike
blocks=(
    01c8 03c1 4501c8 4801c8 4803c1 4d01c8
    29c8 2bc1 4529c8 4829c8 482bc1 4d29c8
    39c8 3bc1 4539c8 4839c8 483bc1 4d39c8
    4801c829c84839c8
)
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/lifted.c"
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/repeated.c"
cmp "$tmp/lifted.c" "$tmp/repeated.c"

cflags=(-std=c11 -O2 -Wall -Wextra -Werror)
if [[ $(uname -m) == x86_64 ]]; then
    cflags+=(-mno-red-zone)
fi
"${CC:-cc}" "${cflags[@]}" -I"$tmp" corpus/m1/arithmetic.c -o "$tmp/arithmetic"
"$tmp/arithmetic" | tee "$evidence/arithmetic.json"
cp "$tmp/lifted.c" "$evidence/"
