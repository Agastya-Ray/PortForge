#!/usr/bin/env bash
set -euo pipefail

evidence=${1:?usage: check_pe_console.sh <evidence> [reference-evidence] [clang|mingw|msvc]}
reference=${2:-}
compiler=${3:-clang}
case "$compiler" in
    clang|mingw|msvc) ;;
    *) printf 'Unknown console compiler: %s\n' "$compiler" >&2; exit 2 ;;
esac
arch=$(uname -m)
if [[ $arch == aarch64 || $compiler == msvc ]]; then
    reference=${2:?missing native console evidence}
fi
printf 'Console corpus using %s executables on %s\n' "$compiler" "$arch"
mkdir -p "$evidence"
evidence=$(realpath "$evidence")
tmp=$(mktemp -d)
mkdir -m 700 "$tmp/runtime"
export XDG_RUNTIME_DIR="$tmp/runtime"
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
if ! timeout 120 "$loader" wineboot.exe --init > "$evidence/wineboot.log" 2>&1; then
    cat "$evidence/wineboot.log" >&2
    exit 1
fi

run() {
    local input=$1 output=$2 status=0
    shift 2
    mkdir -p "$output"
    timeout 120 "$loader" "$@" < "$input" > "$output/stdout.bin" 2> "$output/stderr.bin" || status=$?
    printf '%s\n' "$status" > "$output/exit.txt"
}

target=()
suffix=.exe.so
if [[ $arch == aarch64 ]]; then
    target=(--target=aarch64-windows)
    suffix=.exe
elif [[ $compiler == clang ]]; then
    { "${WINDOWS_CC:-clang}" --version; "${LLD_LINK:-lld-link}" --version; } > "$evidence/toolchain.txt"
    "${LLD_LINK:-lld-link}" /lib /def:corpus/m1/pe-console-kernel32.def /machine:x64 /out:"$tmp/kernel32.lib"
elif [[ $compiler == mingw ]]; then
    "${MINGW_CC:-x86_64-w64-mingw32-gcc}" --version > "$evidence/toolchain.txt"
else
    cp "$reference/toolchain.txt" "$evidence/toolchain.txt"
fi

build_guest() {
    local source=$1 output=$2
    case "$compiler" in
        clang)
            "${WINDOWS_CC:-clang}" --target=x86_64-w64-windows-gnu -ffreestanding -O1 \
                -fno-unroll-loops -fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror \
                -c "$source" -o "$tmp/guest.obj"
            "${LLD_LINK:-lld-link}" /entry:pf_entry /subsystem:console /nodefaultlib /timestamp:0 \
                /out:"$output" "$tmp/guest.obj" "$tmp/kernel32.lib"
            ;;
        mingw)
            "${MINGW_CC:-x86_64-w64-mingw32-gcc}" -ffreestanding -O1 -fno-unroll-loops \
                -fno-tree-vectorize -fno-stack-protector -Wall -Wextra -Werror -nostdlib \
                -Wl,--entry,pf_entry,--subsystem,console,--no-insert-timestamp \
                "$source" -lkernel32 -o "$output"
            ;;
    esac
}

compare_result() {
    local output
    for output in stdout.bin stderr.bin exit.txt; do
        if ! cmp "$1/$output" "$2/$output"; then
            python3 - "$1/$output" "$2/$output" <<'PY'
from pathlib import Path
import sys
for name in sys.argv[1:]:
    data = Path(name).read_bytes()
    print(f"{name}: {len(data)} bytes, first 4096 bytes {data[:4096]!r}", file=sys.stderr)
PY
            return 1
        fi
    done
}

for name in text count streams calls input clock system; do
    work="$evidence/$name"
    mkdir -p "$work"
    python3 corpus/m1/check_pe_console_output.py "$name" "$work" --prepare-input
    if [[ $arch == x86_64 ]]; then
        if [[ $compiler == msvc ]]; then
            cp "$reference/$name/guest.exe" "$work/guest.exe"
            cmp "$work/stdin.bin" "$reference/$name/stdin.bin"
        else
            build_guest "corpus/m1/pe-console-$name.c" "$work/guest.exe"
            build_guest "corpus/m1/pe-console-$name.c" "$tmp/repeated.exe"
            cmp "$work/guest.exe" "$tmp/repeated.exe"
        fi
        run "$work/stdin.bin" "$work/native" "$work/guest.exe"
        oracle="$work/native"
        if [[ $compiler == msvc ]]; then
            compare_result "$oracle" "$reference/$name/native"
        fi
    else
        cp "$reference/$name/guest.exe" "$work/guest.exe"
        cmp "$work/stdin.bin" "$reference/$name/stdin.bin"
        oracle="$reference/$name/native"
    fi

    "$lift" --pe "$work/guest.exe" > "$work/lifted.c"
    "$lift" --pe "$work/guest.exe" > "$tmp/repeated.c"
    cmp "$work/lifted.c" "$tmp/repeated.c"
    "$lift" --pe-image "$work/guest.exe" > "$work/image.bin"
    "$lift" --pe-image "$work/guest.exe" > "$tmp/repeated.bin"
    cmp "$work/image.bin" "$tmp/repeated.bin"
    if [[ $name == text && $compiler == clang ]]; then
        "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$work" \
            corpus/m1/pe-console-guards.c -o "$tmp/guards"
        "$tmp/guards"
    fi
    if [[ $arch == aarch64 ]]; then
        cmp "$work/lifted.c" "$reference/$name/lifted.c"
        cmp "$work/image.bin" "$reference/$name/image.bin"
    fi
    "$winegcc" "${target[@]}" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$work" \
        runtime/host/wine_console.c -o "$tmp/check.exe"
    run "$work/stdin.bin" "$work/translated" "$tmp/check$suffix" "$work/image.bin"
    compare_result "$work/translated" "$oracle"
    python3 corpus/m1/check_pe_console_output.py "$name" "$work/translated"
done
