#!/usr/bin/env bash
set -euo pipefail
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cargo build --release --locked --offline -p pf-ir-spike
python3 corpus/m1/scalar_storage_cases.py "$tmp" target/release/pf-ir-spike
extra=()
if [[ $(uname -m) == x86_64 ]]; then extra=(-mno-red-zone); fi
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "${extra[@]}" -I"$tmp" corpus/m1/scalar_storage.c -o "$tmp/check"
"$tmp/check" > "$tmp/digest"
digest=$(cat "$tmp/digest")
[[ "$digest" =~ ^[0-9a-f]{16}$ ]]
printf '%s\n' "$digest" | cmp -s - "$tmp/digest"
cat "$tmp/digest"
