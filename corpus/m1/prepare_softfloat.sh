#!/usr/bin/env bash
set -euo pipefail

work=${1:?usage: prepare_softfloat.sh <work-directory>}
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
cp "$source_dir/build/Linux-x86_64-GCC/"{Makefile,platform.h} "$work/build/"
make -s -j "${PF_BUILD_JOBS:-$(nproc)}" -C "$work/build" \
    SOURCE_DIR="$source_dir/source" SPECIALIZE_TYPE=8086-SSE \
    'SOFTFLOAT_OPTS=-DTHREAD_LOCAL=_Thread_local -DSOFTFLOAT_ROUND_ODD -DINLINE_LEVEL=5 -DSOFTFLOAT_FAST_DIV32TO16 -DSOFTFLOAT_FAST_DIV64TO32'
