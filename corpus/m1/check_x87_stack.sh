#!/usr/bin/env bash
set -euo pipefail

work=${1:?usage: check_x87_stack.sh <softfloat-directory> <evidence-directory>}
evidence=${2:?missing evidence directory}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"
cargo build --release --locked --offline -p pf-ir-spike
blocks=(d9e8 d9ee d9e0 d9e1 d8c1 dcc1 dec1 d8c3 dec0 01c8d8c1d9e8 d9e8d9e8dec1d9e0)
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/lifted.c"
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/repeated.c"
cmp "$tmp/lifted.c" "$tmp/repeated.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Iruntime/guest -I"$tmp" \
    -I"$work/source/source/include" corpus/m1/x87_stack.c runtime/guest/{x87,x87_stack}.c \
    "$work/build/softfloat.a" -o "$tmp/x87-stack"
"$tmp/x87-stack" | tee "$evidence/x87-stack.json"
cp "$tmp/lifted.c" "$evidence/x87-lifted.c"
