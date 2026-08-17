#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Format gate: every tracked C++ source matches .clang-format. Reports the
# offending files and exits non-zero; it never rewrites anything, so it is
# safe to run on a dirty tree. To actually reformat, run clang-format -i
# over the files it names.
#
# Set CLANG_FORMAT to point at a specific binary. The version that decides
# the outcome is pinned in .llvm-version — a formatter disagrees with
# itself across major versions, and a gate whose verdict depends on which
# one you installed is not a gate.
set -euo pipefail
cd "$(dirname "$0")/.."

: "${CLANG_FORMAT:=clang-format}"

if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
  echo "check-format: '$CLANG_FORMAT' not found." >&2
  echo "  pip install clang-format==$(cat .llvm-version)" >&2
  echo "  (or set CLANG_FORMAT to a binary of that version)" >&2
  exit 127
fi

# Advisory, not fatal: a nearby version is still worth running, you just
# cannot conclude anything from it about what CI will say.
want=$(cat .llvm-version)
got=$("$CLANG_FORMAT" --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [ "$got" != "$want" ]; then
  echo "check-format: WARNING: clang-format $got, but the gate is $want;" >&2
  echo "  a clean run here does not mean a clean run in CI." >&2
fi

# Not '*.h.in': version.h.in is a configure_file template whose @VAR@
# placeholders are not C++ and which clang-format cannot parse. The file
# it generates is formatted, and that is the one that gets compiled.
mapfile -t files < <(git ls-files '*.c' '*.cc' '*.cpp' '*.h' '*.hpp')

if [ "${#files[@]}" -eq 0 ]; then
  echo "check-format: no C++ sources tracked" >&2
  exit 1
fi

"$CLANG_FORMAT" --dry-run -Werror "${files[@]}"
echo "check-format: OK (${#files[@]} files, clang-format $got)"
