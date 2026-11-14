#!/usr/bin/env bash
set -euo pipefail

work=${1:?usage: prepare_softfloat.sh <work-directory>}
mode=${2:-native}
case "$mode" in
    native) compiler="${CC:-gcc} -fPIC"; archiver=${AR:-ar} ;;
    windows-arm64)
        crt=${WINE_MSVCRT_INCLUDE:-/usr/include/wine/wine/msvcrt}
        compiler="${WINDOWS_CC:-clang} --target=aarch64-windows-msvc -mno-incremental-linker-compatible -ffreestanding -isystem $crt -isystem ${crt%/msvcrt}/windows"
        archiver=${LLVM_AR:-llvm-ar}
        ;;
    *) echo "unknown SoftFloat ABI: $mode" >&2; exit 2 ;;
esac
revision=07ebd29db0cfb55723a79fbbffe99103ef38e305
mkdir -p "$work"
work=$(cd "$work" && pwd)
source_dir="$work/source"
if [[ ! -d "$source_dir/.git" ]]; then
    git init -q "$source_dir"
    git -C "$source_dir" remote add origin https://github.com/ucb-bar/berkeley-softfloat-3.git
    git -C "$source_dir" fetch -q --depth 1 origin "$revision"
    git -C "$source_dir" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$source_dir" rev-parse HEAD)" = "$revision"

# This upstream platform header describes little endian, 64-bit GCC targets
# with native int128. The selected specialization is for the guest x87.
mkdir -p "$work/build"
if [[ -f "$work/build/abi.txt" ]] && [[ $(cat "$work/build/abi.txt") != "$mode" ]]; then
    echo "SoftFloat directory contains another ABI" >&2; exit 1
fi
config="$mode|$compiler|$archiver"
if [[ ! -f "$work/build/compiler.txt" ]] || [[ $(cat "$work/build/compiler.txt") != "$config" ]]; then
    rm -f "$work/build/"*.o "$work/build/softfloat.a"
fi
printf '%s\n' "$config" > "$work/build/compiler.txt"
printf '%s\n' "$mode" > "$work/build/abi.txt"
cp "$source_dir/build/Linux-x86_64-GCC/"{Makefile,platform.h} "$work/build/"
make -s -j "${PF_BUILD_JOBS:-$(nproc)}" -C "$work/build" \
    SOURCE_DIR="$source_dir/source" SPECIALIZE_TYPE=8086-SSE \
    COMPILE_C="$compiler -c -Werror-implicit-function-declaration -DSOFTFLOAT_FAST_INT64 \$(SOFTFLOAT_OPTS) \$(C_INCLUDES) -O2 -o \$@" \
    MAKELIB="$archiver crsD \$@" \
    'SOFTFLOAT_OPTS=-DTHREAD_LOCAL=_Thread_local -DSOFTFLOAT_ROUND_ODD -DINLINE_LEVEL=5 -DSOFTFLOAT_FAST_DIV32TO16 -DSOFTFLOAT_FAST_DIV64TO32'
