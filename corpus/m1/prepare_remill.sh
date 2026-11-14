#!/usr/bin/env bash
set -euo pipefail

work=${1:?usage: prepare_remill.sh <work-directory> [headers]}
mode=${2:-tools}
case "$mode" in
    tools|headers) ;;
    *) echo "unsupported preparation mode: $mode" >&2; exit 2 ;;
esac
mkdir -p "$work"
work=$(cd "$work" && pwd)

checkout() {
    local path=$1 url=$2 revision=$3
    if [[ ! -d "$path/.git" ]]; then
        git init -q "$path"
        git -C "$path" remote add origin "$url"
        git -C "$path" fetch -q --depth 1 origin "$revision"
        git -C "$path" checkout -q --detach FETCH_HEAD
    fi
    [[ $(git -C "$path" rev-parse HEAD) == "$revision" ]]
}

checkout "$work/remill" https://github.com/lifting-bits/remill.git \
    0e324aee8c67a63ec759ef379dcfafa0b3cb1448
patch_file="$(cd "$(dirname "$0")" && pwd)/remill-signed-subtraction.patch"
if ! git -C "$work/remill" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    git -C "$work/remill" apply --check "$patch_file"
    git -C "$work/remill" apply "$patch_file"
fi
if [[ "$mode" == headers ]]; then
    exit 0
fi
checkout "$work/llvm-cbe" https://github.com/JuliaHubOSS/llvm-cbe.git \
    21569b994b019c711c5631edf1e7273332138f3d

# Repair unsigned zero literals and signed vector lanes in this pinned CBE revision.
for patch_name in remill-cbe-unsigned-min.patch remill-cbe-signed-lanes.patch; do
    patch_file="$(cd "$(dirname "$0")" && pwd)/$patch_name"
    if ! git -C "$work/llvm-cbe" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        git -C "$work/llvm-cbe" apply --check "$patch_file"
        git -C "$work/llvm-cbe" apply "$patch_file"
    fi
done

export CC=clang-19 CXX=clang++-19
llvm_dir=$(llvm-config-19 --cmakedir)
jobs=${PF_BUILD_JOBS:-$(nproc)}
cmake -G Ninja -S "$work/remill/dependencies" -B "$work/deps-build" \
    -DUSE_EXTERNAL_LLVM=ON -DLLVM_DIR="$llvm_dir" -DCMAKE_INSTALL_PREFIX="$work/deps"
cmake --build "$work/deps-build" --parallel "$jobs"
cmake -G Ninja -S "$work/remill" -B "$work/remill-build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$work/deps" -DLLVM_DIR="$llvm_dir" -DREMILL_ENABLE_TESTING=OFF
cmake --build "$work/remill-build" --parallel "$jobs" --target remill-lift-19 amd64
cmake -G Ninja -S "$work/llvm-cbe" -B "$work/cbe-build" -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR="$llvm_dir" -DLLVM_INCLUDE_TESTS=OFF
cmake --build "$work/cbe-build" --parallel "$jobs"

bash "$(dirname "$0")/check_remill_cbe.sh" "$work/cbe-build/tools/llvm-cbe/llvm-cbe"
