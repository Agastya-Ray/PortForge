#!/usr/bin/env bash
set -euo pipefail
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
python3 corpus/m1/wide_integer_cases.py "$tmp" target/release/pf-ir-spike
extra=()
if [[ $(uname -m) == x86_64 ]]; then extra=(-mno-red-zone); fi
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "${extra[@]}" -I"$tmp" corpus/m1/wide_integer.c -o "$tmp/check"
"$tmp/check" > "$tmp/digest"
for name in zero overflow valid; do
    fault=0
    if [[ $name == zero ]]; then fault=1; fi
    if [[ $name == overflow ]]; then fault=2; fi
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -DEXPECT_FAULT="$fault" -I"$tmp/$name" corpus/m1/wide_integer_program.c -o "$tmp/program"
    "$tmp/program"
done
digest=$(cat "$tmp/digest")
[[ "$digest" =~ ^[0-9a-f]{16}$ ]]
printf '%s\n' "$digest" | cmp -s - "$tmp/digest"
cat "$tmp/digest"
