#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_sse2.sh <evidence-directory>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"
cargo build --release --locked --offline -p pf-ir-spike
source corpus/m1/sse2_blocks.sh
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/lifted.c"
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/repeated.c"
cmp "$tmp/lifted.c" "$tmp/repeated.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$tmp" corpus/m1/sse2.c -o "$tmp/sse2"
"$tmp/sse2" | tee "$evidence/sse2.json"
if [[ $(uname -m) == aarch64 ]]; then
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -DPF_PORTABLE_ONLY \
        -I"$tmp" corpus/m1/sse2.c -o "$tmp/sse2-portable"
    "$tmp/sse2-portable" | tee "$evidence/sse2-portable.json"
    cmp "$evidence/sse2.json" "$evidence/sse2-portable.json"
fi
cp "$tmp/lifted.c" "$evidence/sse2-lifted.c"
