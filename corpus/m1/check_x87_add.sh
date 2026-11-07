#!/usr/bin/env bash
set -euo pipefail

work=${1:?usage: check_x87_add.sh <softfloat-directory> <evidence-directory>}
evidence=${2:?missing evidence directory}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$evidence"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Iruntime/guest -I"$work/source/source/include" \
    corpus/m1/x87_add.c runtime/guest/x87.c "$work/build/softfloat.a" -o "$tmp/x87-add"
"$tmp/x87-add" | tee "$evidence/x87-add.json"
