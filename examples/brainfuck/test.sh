#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$case_dir/../.." && pwd)"
executable="$repo_root/build/brainfuck/brainfuck"
temporary_dir="$(mktemp -d)"
trap 'rm -rf "$temporary_dir"' EXIT

"$case_dir/build.sh"

"$repo_root/build/ccompiler" "$case_dir/tests/compiler-goto.c" \
  "$temporary_dir/compiler-goto.ll"
llvm-as "$temporary_dir/compiler-goto.ll" -o "$temporary_dir/compiler-goto.bc"
set +e
lli "$temporary_dir/compiler-goto.bc"
goto_status=$?
set -e
if [[ $goto_status -ne 3 ]]; then
  echo "compiler goto test: expected lli status 3, got $goto_status" >&2
  exit 1
fi

"$executable" "$case_dir/tests/hello.bf" >"$temporary_dir/hello.actual"
cmp "$case_dir/tests/hello.expected" "$temporary_dir/hello.actual"

"$executable" "$case_dir/tests/echo.bf" \
  <"$case_dir/tests/echo.in" >"$temporary_dir/echo.actual"
cmp "$case_dir/tests/echo.expected" "$temporary_dir/echo.actual"

set +e
timeout 2 "$executable" "$case_dir/tests/unmatched.bf" \
  >"$temporary_dir/unmatched.actual" 2>"$temporary_dir/unmatched.err"
unmatched_status=$?
set -e
if [[ $unmatched_status -eq 0 || $unmatched_status -eq 124 ]]; then
  echo "unmatched bracket test: unexpected status $unmatched_status" >&2
  exit 1
fi
grep -q "unmatched bracket" "$temporary_dir/unmatched.err"

set +e
timeout 2 "$executable" "$case_dir/tests/unmatched-close.bf" \
  >"$temporary_dir/unmatched-close.actual" \
  2>"$temporary_dir/unmatched-close.err"
unmatched_close_status=$?
set -e
if [[ $unmatched_close_status -eq 0 || $unmatched_close_status -eq 124 ]]; then
  echo "unmatched closing bracket test: unexpected status $unmatched_close_status" >&2
  exit 1
fi
grep -q "unmatched bracket" "$temporary_dir/unmatched-close.err"

set +e
"$executable" "$case_dir/tests/bounds.bf" \
  >"$temporary_dir/bounds.actual" 2>"$temporary_dir/bounds.err"
bounds_status=$?
set -e
if [[ $bounds_status -eq 0 ]]; then
  echo "bounds test: expected a nonzero status" >&2
  exit 1
fi
grep -q "data pointer out of bounds" "$temporary_dir/bounds.err"

printf '%0.s>' {1..30001} >"$temporary_dir/upper-bound.bf"
set +e
"$executable" "$temporary_dir/upper-bound.bf" \
  >"$temporary_dir/upper-bound.actual" 2>"$temporary_dir/upper-bound.err"
upper_bound_status=$?
set -e
if [[ $upper_bound_status -eq 0 ]]; then
  echo "upper bounds test: expected a nonzero status" >&2
  exit 1
fi
grep -q "data pointer out of bounds" "$temporary_dir/upper-bound.err"

printf -- '-.' >"$temporary_dir/underflow.bf"
"$executable" "$temporary_dir/underflow.bf" \
  >"$temporary_dir/underflow.actual"
if [[ "$(od -An -tu1 "$temporary_dir/underflow.actual" | tr -d '[:space:]')" != "255" ]]; then
  echo "cell underflow test: expected byte 255" >&2
  exit 1
fi

printf '%0.s+' {1..256} >"$temporary_dir/overflow.bf"
printf '.' >>"$temporary_dir/overflow.bf"
"$executable" "$temporary_dir/overflow.bf" >"$temporary_dir/overflow.actual"
if [[ "$(od -An -tu1 "$temporary_dir/overflow.actual" | tr -d '[:space:]')" != "0" ]]; then
  echo "cell overflow test: expected byte 0" >&2
  exit 1
fi

set +e
"$executable" >"$temporary_dir/usage.actual" 2>"$temporary_dir/usage.err"
usage_status=$?
set -e
if [[ $usage_status -eq 0 ]]; then
  echo "usage test: expected a nonzero status" >&2
  exit 1
fi
grep -q "usage: brainfuck" "$temporary_dir/usage.err"

set +e
"$executable" "$temporary_dir/does-not-exist.bf" \
  >"$temporary_dir/missing.actual" 2>"$temporary_dir/missing.err"
missing_status=$?
set -e
if [[ $missing_status -eq 0 ]]; then
  echo "missing file test: expected a nonzero status" >&2
  exit 1
fi
grep -q "cannot open program file" "$temporary_dir/missing.err"
