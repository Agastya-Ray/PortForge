#!/usr/bin/env bash
set -euo pipefail

output=${1:?usage: emit_remill.sh <output-directory>}
: "${REMILL_LIFT:?set REMILL_LIFT to the Remill executable}"
: "${LLVM_CBE:?set LLVM_CBE to the LLVM C backend executable}"
: "${LLVM_LINK:=llvm-link-19}"

source corpus/m1/blocks.sh
mkdir -p "$output"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
modules=()
for index in "${!blocks[@]}"; do
    address=$((4096 + 256 * index))
    module="$tmp/trace-$index.bc"
    "$REMILL_LIFT" --os windows --arch amd64 --address "$address" \
        --bytes "${blocks[index]}" --bc_out "$module"
    modules+=("$module")
    printf 'PF_TRACE(%d, sub_%x)\n' "$index" "$address"
done > "$output/remill_entries.inc"
"$LLVM_LINK" "${modules[@]}" -o "$tmp/arithmetic.bc"
"$LLVM_CBE" "$tmp/arithmetic.bc" -o "$output/remill.c"
