#!/usr/bin/env bash
set -euo pipefail
cbe=${1:?usage: check_remill_cbe.sh <llvm-cbe>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 - "$tmp/cases.ll" <<'PY'
from pathlib import Path
import sys
lines = ['target triple = "x86_64-unknown-linux-gnu"']
for bits in (8, 16, 32, 64):
    lanes = 128 // bits
    scalar, vector = f'i{bits}', f'<{lanes} x i{bits}>'
    lines += [f'declare {scalar} @llvm.usub.sat.i{bits}({scalar}, {scalar})',
              f'declare {vector} @llvm.usub.sat.v{lanes}i{bits}({vector}, {vector})']
    for packed in (False, True):
        name = f'pf_cbe_{"vector" if packed else "scalar"}{bits}'
        lines += [f'define i64 @{name}(i64 %left, i64 %right) {{']
        if bits != 64:
            lines += [f'  %a = trunc i64 %left to {scalar}', f'  %b = trunc i64 %right to {scalar}']
            a, b = '%a', '%b'
        else:
            a, b = '%left', '%right'
        if packed:
            lines += [f'  %va = insertelement {vector} zeroinitializer, {scalar} {a}, i32 {lanes - 1}',
                      f'  %vb = insertelement {vector} zeroinitializer, {scalar} {b}, i32 {lanes - 1}',
                      f'  %v = call {vector} @llvm.usub.sat.v{lanes}i{bits}({vector} %va, {vector} %vb)',
                      f'  %result = extractelement {vector} %v, i32 {lanes - 1}']
        else:
            lines += [f'  %result = call {scalar} @llvm.usub.sat.i{bits}({scalar} {a}, {scalar} {b})']
        if bits != 64:
            lines += [f'  %extended = zext {scalar} %result to i64', '  ret i64 %extended']
        else:
            lines += ['  ret i64 %result']
        lines += ['}']

# These operations preserve signed interpretation even though CBE vector storage is unsigned.
for bits in (8, 16, 32, 64):
    vector = f'<2 x i{bits}>'
    for operation in ('ashr', 'slt', 'sge'):
        lines += [f'define i64 @pf_cbe_{operation}{bits}(i64 %left, i64 %right) {{']
        if bits != 64:
            lines += [f'  %a = trunc i64 %left to i{bits}', f'  %b = trunc i64 %right to i{bits}']
            a, b = '%a', '%b'
        else:
            a, b = '%left', '%right'
        lines += [f'  %va = insertelement {vector} zeroinitializer, i{bits} {a}, i32 1',
                  f'  %vb = insertelement {vector} zeroinitializer, i{bits} {b}, i32 1']
        result_bits = bits if operation == 'ashr' else 1
        if operation == 'ashr':
            lines += [f'  %v = ashr {vector} %va, %vb']
        else:
            lines += [f'  %v = icmp {operation} {vector} %va, %vb']
        lines += [f'  %result = extractelement <2 x i{result_bits}> %v, i32 1']
        if result_bits != 64:
            lines += [f'  %extended = zext i{result_bits} %result to i64', '  ret i64 %extended']
        else:
            lines += ['  ret i64 %result']
        lines += ['}']
for bits, target in ((1, 64), (8, 16), (16, 32), (32, 64)):
    lines += [f'define i64 @pf_cbe_extend{bits}(i64 %left, i64 %ignored) {{',
              f'  %a = trunc i64 %left to i{bits}',
              f'  %va = insertelement <2 x i{bits}> zeroinitializer, i{bits} %a, i32 1',
              f'  %v = sext <2 x i{bits}> %va to <2 x i{target}>',
              f'  %result = extractelement <2 x i{target}> %v, i32 1']
    if target != 64:
        lines += [f'  %extended = zext i{target} %result to i64', '  ret i64 %extended']
    else:
        lines += ['  ret i64 %result']
    lines += ['}']

lines += ['define i64 @pf_cbe_bit_slt(i64 %left, i64 %right) {',
          '  %a = trunc i64 %left to i1', '  %b = trunc i64 %right to i1',
          '  %va = insertelement <2 x i1> zeroinitializer, i1 %a, i32 1',
          '  %vb = insertelement <2 x i1> zeroinitializer, i1 %b, i32 1',
          '  %v = icmp slt <2 x i1> %va, %vb',
          '  %result = extractelement <2 x i1> %v, i32 1',
          '  %extended = zext i1 %result to i64', '  ret i64 %extended', '}']
Path(sys.argv[1]).write_text('\n'.join(lines) + '\n')
PY
"$cbe" "$tmp/cases.ll" -o "$tmp/cases.c"
"${CC:-cc}" -std=gnu11 -O2 -Duint128_t=__uint128_t -c "$tmp/cases.c" -o "$tmp/cases.o"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror corpus/m1/remill_cbe_regression.c "$tmp/cases.o" -o "$tmp/check"
"$tmp/check"
