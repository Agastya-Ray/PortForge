#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_console.sh <evidence> [reference-evidence]}
reference=${2:-}
mkdir -p "$evidence"
evidence=$(realpath "$evidence")
tmp=$(mktemp -d)
cleanup() {
    if [[ -d "$tmp/prefix" ]]; then
        "${WINESERVER:-/usr/lib/wine/wineserver}" -k >/dev/null 2>&1 || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT
cargo build --release --locked --offline -p pf-ir-spike
lift=target/release/pf-ir-spike
winegcc=${WINEGCC:-winegcc}
if [[ -z ${WINEGCC:-} ]] && ! command -v "$winegcc" >/dev/null; then winegcc=winegcc-stable; fi
loader=${WINELOADER:-/usr/lib/wine/wine64}
export WINEPREFIX="$tmp/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
if ! timeout 120 "$loader" wineboot.exe --init > "$tmp/wineboot.log" 2>&1; then
    cat "$tmp/wineboot.log" >&2
    exit 1
fi

run() {
    local output=$1 status=0
    shift
    mkdir -p "$output"
    timeout 120 "$loader" "$@" > "$output/stdout.bin" 2> "$output/stderr.bin" || status=$?
    printf '%s\n' "$status" > "$output/exit.txt"
}

target=()
suffix=.exe.so
if [[ $(uname -m) == aarch64 ]]; then
    reference=${2:?missing x86 console evidence}
    target=(--target=aarch64-windows)
    suffix=.exe
else
    "${LLD_LINK:-lld-link}" /lib /def:corpus/m1/pe-console-kernel32.def /machine:x64 /out:"$tmp/kernel32.lib"
fi

for name in text count streams; do
    work="$evidence/$name"
    mkdir -p "$work"
    if [[ $(uname -m) == x86_64 ]]; then
        "${WINDOWS_CC:-clang}" --target=x86_64-w64-windows-gnu -ffreestanding -O1 \
            -fno-unroll-loops -fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror \
            -c "corpus/m1/pe-console-$name.c" -o "$tmp/guest.obj"
        for executable in "$work/guest.exe" "$tmp/repeated.exe"; do
            "${LLD_LINK:-lld-link}" /entry:pf_entry /subsystem:console /nodefaultlib /timestamp:0 \
                /out:"$executable" "$tmp/guest.obj" "$tmp/kernel32.lib"
        done
        cmp "$work/guest.exe" "$tmp/repeated.exe"
        run "$work/native" "$work/guest.exe"
        oracle="$work/native"
    else
        cp "$reference/$name/guest.exe" "$work/guest.exe"
        oracle="$reference/$name/native"
    fi

    "$lift" --pe "$work/guest.exe" > "$work/lifted.c"
    "$lift" --pe "$work/guest.exe" > "$tmp/repeated.c"
    cmp "$work/lifted.c" "$tmp/repeated.c"
    "$lift" --pe-image "$work/guest.exe" > "$work/image.bin"
    "$lift" --pe-image "$work/guest.exe" > "$tmp/repeated.bin"
    cmp "$work/image.bin" "$tmp/repeated.bin"
    if [[ $name == text ]]; then
        "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$work" \
            corpus/m1/pe-console-guards.c -o "$tmp/guards"
        "$tmp/guards"
    fi
    if [[ $(uname -m) == aarch64 ]]; then
        cmp "$work/lifted.c" "$reference/$name/lifted.c"
        cmp "$work/image.bin" "$reference/$name/image.bin"
    fi
    "$winegcc" "${target[@]}" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$work" \
        runtime/host/wine_console.c -o "$tmp/check.exe"
    run "$work/translated" "$tmp/check$suffix" "$work/image.bin"
    for output in stdout.bin stderr.bin exit.txt; do
        cmp "$work/translated/$output" "$oracle/$output"
    done
    python3 - "$name" "$work/translated" <<'PY'
from pathlib import Path
import struct
import sys

name, result = sys.argv[1:]
root = Path(result)
value = 0x531c7ab2
words = []
for _ in range(8):
    value = ((value + 0x92c7a1e5) & 0xffffffff) ^ 0x4107332f
    if value > 0xc0000000:
        value ^= 0x7010f0aa
    words.append(value)
stdout, stderr, status = {
    "text": (b"PortForge console\r\nsecond line\n", b"", b"0\n"),
    "count": (struct.pack("<8I", *words), b"", b"0\n"),
    "streams": (bytes.fromhex("00 ff 0d 0a 00 1a"), b"guest diagnostic\r\n", b"23\n"),
}[name]
assert (root / "stdout.bin").read_bytes() == stdout
assert (root / "stderr.bin").read_bytes() == stderr
assert (root / "exit.txt").read_bytes() == status
print(f"{name}: {len(stdout)} stdout bytes, {len(stderr)} stderr bytes, exit {int(status)}")
PY
done
