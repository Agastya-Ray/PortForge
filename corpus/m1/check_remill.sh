#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:?usage: check_remill.sh <remill-source> <emitted-c-directory> <evidence-directory>}
generated=${2:?missing emitted C directory}
evidence=${3:?missing evidence directory}
suite=${4:-arithmetic}
case "$suite" in
    arithmetic) c_size=remill-c-bytes.txt ;;
    sse2) c_size=remill-sse2-c-bytes.txt ;;
    *) echo "unsupported comparison suite: $suite" >&2; exit 2 ;;
esac
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"

cflags=(-std=c11 -O2 -Wall -Wextra -Werror)
if [[ $(uname -m) == x86_64 ]]; then
    cflags+=(-mno-red-zone)
fi
"${CC:-cc}" -std=gnu11 -O2 -Duint128_t=__uint128_t \
    -c "$generated/remill.c" -o "$tmp/remill.o"
"${CXX:-clang++-19}" -std=c++17 -O2 -DADDRESS_SIZE_BITS=64 \
    -I"$source_dir/include" -I"$generated" -c corpus/m1/remill_adapter.cpp -o "$tmp/adapter.o"
"${CC:-cc}" "${cflags[@]}" -DPF_USE_REMILL -I"$generated" \
    -c "corpus/m1/$suite.c" -o "$tmp/arithmetic.o"
"${CXX:-clang++-19}" "$tmp/remill.o" "$tmp/adapter.o" "$tmp/arithmetic.o" -o "$tmp/arithmetic"
"$tmp/arithmetic" | tee "$evidence/remill-$suite.json"
wc -c < "$generated/remill.c" > "$evidence/$c_size"
if [[ $suite == arithmetic && ${PF_BENCHMARK:-0} == 1 ]]; then
    for repetition in {1..3}; do
        "$tmp/arithmetic" --benchmark
    done | tee "$evidence/remill-benchmark.jsonl"
fi
