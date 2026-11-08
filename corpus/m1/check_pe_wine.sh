#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_wine.sh <evidence> [reference-evidence]}
mkdir -p "$evidence"
tmp=$(mktemp -d)
cleanup() {
    if [[ -d "$tmp/prefix" ]]; then
        "${WINESERVER:-/usr/lib/wine/wineserver}" -k >/dev/null 2>&1 || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT
cargo build --release --locked --offline -p pf-ir-spike
if [[ $(uname -m) == x86_64 ]]; then
    as --64 --defsym PF_WINE=1 corpus/m1/pe-import.s -o "$tmp/import.o"
    ld -mi386pep --entry pf_fixture --subsystem console --image-base 0x140000000 \
        --no-insert-timestamp "$tmp/import.o" -o "$tmp/template.exe"
    python3 corpus/m1/pe_import_fixture.py "$tmp/template.exe" "$evidence/wine-fixture.exe" "$tmp/image.bin" wine
else
    reference=${2:?missing x86 reference evidence}
    cp "$reference/wine-fixture.exe" "$evidence/wine-fixture.exe"
fi
target/release/pf-ir-spike --pe "$evidence/wine-fixture.exe" > "$evidence/lifted.c"
target/release/pf-ir-spike --pe "$evidence/wine-fixture.exe" > "$tmp/repeated.c"
cmp "$evidence/lifted.c" "$tmp/repeated.c"

winegcc=${WINEGCC:-winegcc}
if [[ -z ${WINEGCC:-} ]] && ! command -v "$winegcc" >/dev/null; then winegcc=winegcc-stable; fi
loader=${WINELOADER:-/usr/lib/wine/wine64}
target=()
executable="$tmp/check.exe.so"
if [[ $(uname -m) == aarch64 ]]; then
    # Wine's ELF Winelib output uses an x86-specific module prologue.
    target=(--target=aarch64-windows)
    executable="$tmp/check.exe"
fi
export WINEPREFIX="$tmp/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
"$winegcc" "${target[@]}" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$PWD" -I"$evidence" \
    corpus/m1/pe-wine.c -o "$tmp/check.exe"
"$loader" --version | tee "$evidence/wine-version.txt"
timeout 120 "$loader" "$executable" | tee "$evidence/digest.txt"
[[ $(cat "$evidence/digest.txt") =~ ^[0-9a-f]{16}$ ]]
if [[ $(uname -m) != x86_64 ]]; then
    cmp "$evidence/lifted.c" "$reference/lifted.c"
    cmp "$evidence/digest.txt" "$reference/digest.txt"
fi
