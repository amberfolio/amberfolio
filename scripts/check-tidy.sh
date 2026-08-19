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

# One analysis per file, not one per configuration.
#
# The presets use Ninja Multi-Config, so a configured tree holds three
# entries per source — Debug, Release, RelWithDebInfo — and clang-tidy
# runs on every entry that names the file it was handed. That turns 48
# files into 144 analyses producing the same findings three times: the
# check set in .clang-tidy is not configuration-sensitive, and there is
# no NDEBUG-dependent code in the tree to make it so (the only asserts
# here are static_asserts, which do not care about the configuration).
#
# So the database is filtered to one entry per file first. Debug wins
# where there is a choice, because it is the configuration that compiles
# assert() bodies instead of eliding them — if a check ever does depend
# on the configuration, the one that analyses the most code is the one
# worth keeping. A single-config tree has nothing to choose between and
# comes through unchanged.
#
# Python is already what CONTRIBUTING.md assumes: it is how clang-tidy
# itself is installed and how the conformance vectors are fetched. Where
# it is missing this degrades rather than fails — the unfiltered database
# reaches the same verdict, just three times more slowly.
#
# Both spellings, and each one *run* rather than merely looked up: a
# stock Windows puts a stub named python3.exe on the PATH which exists,
# answers `command -v`, and does nothing but advertise the Microsoft
# Store. `command -v python3` is true on such a machine and python3 still
# cannot execute a script, so the only honest test is to execute one.
db="$build"
python_for_json=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1 &&
     "$candidate" -c "" >/dev/null 2>&1; then
    python_for_json="$candidate"
    break
  fi
done

if [ -n "$python_for_json" ]; then
  one_config=$(mktemp -d)
  trap 'rm -rf "$one_config"' EXIT
  "$python_for_json" - \
    "$build/compile_commands.json" \
    "$one_config/compile_commands.json" <<'PYTHON'
import json
import sys

source, target = sys.argv[1], sys.argv[2]
with open(source, encoding="utf-8") as handle:
    entries = json.load(handle)


def rank(entry):
    """Debug first. Anything else, and any tree that names no
    configuration at all, ranks the same and keeps whichever came first."""
    return 0 if "/Debug/" in entry.get("output", "") else 1


kept = {}
for entry in entries:
    name = entry["file"]
    if name not in kept or rank(entry) < rank(kept[name]):
        kept[name] = entry

with open(target, "w", encoding="utf-8") as handle:
    json.dump(list(kept.values()), handle)
PYTHON
  db="$one_config"
else
  echo "check-tidy: no working python, so every configuration is analysed" >&2
  echo "  (the same verdict, roughly three times slower)" >&2
fi

"$CLANG_TIDY" -p "$db" --quiet "${covered[@]}"
echo "check-tidy: OK (${#covered[@]} files, clang-tidy $got, db $build)"
