#!/usr/bin/env bash
set -euo pipefail

suite=${1:?usage: check_memory_ops.sh <suite> [softfloat-directory]}
extra=()
case "$suite" in
    lea|sse2-memory|xmm-moves) ;;
    x87-memory)
        work=${2:?missing SoftFloat directory}
        extra=(-I"$work/source/source/include" runtime/guest/{x87,x87_stack}.c "$work/build/softfloat.a")
        ;;
    *) exit 2 ;;
esac
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
source "corpus/m1/$suite-blocks.sh"
target/release/pf-ir-spike "${blocks[@]}" > "$tmp/lifted.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$tmp" \
    "corpus/m1/$suite.c" "${extra[@]}" -o "$tmp/check"
"$tmp/check"
