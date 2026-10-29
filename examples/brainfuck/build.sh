#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$case_dir/../.." && pwd)"
build_dir="$repo_root/build/brainfuck"
ccompiler="${CCOMPILER:-$repo_root/build/ccompiler}"
host_cc="${HOST_CC:-${CC:-clang}}"

mkdir -p "$build_dir"

"$ccompiler" "$case_dir/main.c" "-o" "$build_dir/main.ll"
llvm-as "$build_dir/main.ll" -o "$build_dir/main.bc"
llc "$build_dir/main.bc" -filetype=obj -o "$build_dir/main.o"
"$host_cc" -std=c11 -O2 -Wall -Wextra -c "$case_dir/host.c" \
  -o "$build_dir/host.o"
"$host_cc" "$build_dir/main.o" "$build_dir/host.o" \
  -o "$build_dir/brainfuck"
