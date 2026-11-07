#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_arithmetic.sh <evidence-directory>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"

cargo build --release --locked --offline -p pf-ir-spike
source corpus/m1/blocks.sh
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
wc -c < "$tmp/lifted.c" > "$evidence/pfir-c-bytes.txt"
if [[ ${PF_BENCHMARK:-0} == 1 ]]; then
    for repetition in {1..3}; do
        "$tmp/arithmetic" --benchmark
    done | tee "$evidence/pfir-benchmark.jsonl"
fi
