#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Static-analysis gate: clang-tidy over every tracked C++ translation unit,
# with the check set and the fail-on-anything policy in .clang-tidy.
#
#     bash scripts/check-tidy.sh [build-dir]     # default build/linux-clang
#
# The build directory only has to be *configured*, not built — clang-tidy
# needs the compile database and the generated headers, both of which
# `cmake --preset linux-clang` produces on its own. Third-party headers
# arrive through -isystem, so nothing in SDL3 or GoogleTest is diagnosed.
#
# Set CLANG_TIDY to point at a specific binary; the pinned version is in
# .llvm-version, for the same reason the formatter is pinned.
set -euo pipefail
cd "$(dirname "$0")/.."

build="${1:-build/linux-clang}"
: "${CLANG_TIDY:=clang-tidy}"

if ! command -v "$CLANG_TIDY" >/dev/null 2>&1; then
  echo "check-tidy: '$CLANG_TIDY' not found." >&2
  echo "  pip install clang-tidy==$(cat .llvm-version)" >&2
  echo "  (or set CLANG_TIDY to a binary of that version)" >&2
  exit 127
fi

if [ ! -f "$build/compile_commands.json" ]; then
  echo "check-tidy: no compile database in '$build'." >&2
  echo "  cmake --preset linux-clang     # then re-run" >&2
  echo "  bash scripts/check-tidy.sh build/<other-preset>" >&2
  exit 1
fi

want=$(cat .llvm-version)
got=$("$CLANG_TIDY" --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [ "$got" != "$want" ]; then
  echo "check-tidy: WARNING: clang-tidy $got, but the gate is $want;" >&2
  echo "  findings differ across major versions." >&2
fi

# Translation units only. Headers are covered through the units that
# include them, filtered by HeaderFilterRegex in .clang-tidy.
mapfile -t files < <(git ls-files '*.c' '*.cc' '*.cpp')

if [ "${#files[@]}" -eq 0 ]; then
  echo "check-tidy: no C++ sources tracked" >&2
  exit 1
fi

# A build tree only holds the targets that build for its toolchain: a
# native database has no entry for the wasm host, and a wasm one has none
# for the SDL host. clang-tidy would fall back to a guessed command line
# for those and report whatever that produced, so drop them here instead —
# and say which, because a file that quietly went unanalysed is the exact
# thing this gate exists to prevent.
# git's spelling of the root, not pwd's: CMake writes native paths into
# the database ("C:/Dev/amberfolio/..."), and so does git rev-parse, while
# pwd under Git Bash would say "/c/Dev/amberfolio" and match nothing.
root=$(git rev-parse --show-toplevel)
covered=()
skipped=()
for f in "${files[@]}"; do
  if grep -qF "\"$root/$f\"" "$build/compile_commands.json"; then
    covered+=("$f")
  else
    skipped+=("$f")
  fi
done

if [ "${#skipped[@]}" -gt 0 ]; then
  echo "check-tidy: not in this build's compile database, skipping:"
  printf '  %s\n' "${skipped[@]}"
  echo "  (run again against a build tree that compiles them)"
fi

if [ "${#covered[@]}" -eq 0 ]; then
  echo "check-tidy: the compile database covers none of the sources" >&2
  exit 1
fi

"$CLANG_TIDY" -p "$build" --quiet "${covered[@]}"
echo "check-tidy: OK (${#covered[@]} files, clang-tidy $got, db $build)"
