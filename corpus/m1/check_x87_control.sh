#!/usr/bin/env bash
set -euo pipefail
evidence=${1:?usage: check_x87_control.sh <evidence-directory>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"
cargo build --release --locked --offline -p pf-ir-spike
target/release/pf-ir-spike dbe3 dbe2 dfe0 d92b d93b dd3b d93bd92bdfe0 > "$tmp/lifted.c"
target/release/pf-ir-spike dbe3 dbe2 dfe0 d92b d93b dd3b d93bd92bdfe0 > "$tmp/repeated.c"
cmp "$tmp/lifted.c" "$tmp/repeated.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$tmp" corpus/m1/x87_control.c -o "$tmp/check"
"$tmp/check" | tee "$evidence/x87-control.json"
