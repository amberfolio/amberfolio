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

# The database is parsed rather than grepped, by python (#39).
#
# Both spellings, and each one *run* rather than merely looked up: a
# stock Windows puts a stub named python3.exe on the PATH which exists,
# answers `command -v`, and does nothing but advertise the Microsoft
# Store. `command -v python3` is true on such a machine and python3 still
# cannot execute a script, so the only honest test is to execute one.
#
# A missing python is a clean stop rather than a degraded run. It used to
# degrade — coverage was decided by grep and only the one-entry-per-file
# filtering below needed python — but a second, hand-rolled path
# normaliser in shell is exactly the thing that would let the two paths
# disagree, and a gate whose verdict depends on which of its two
# implementations ran is not a gate. Python is what CONTRIBUTING.md
# assumes anyway: it is how the pinned clang-tidy is installed, so a
# machine that got past the check above has one.
python_for_db=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1 &&
     "$candidate" -c "" >/dev/null 2>&1; then
    python_for_db="$candidate"
    break
  fi
done

if [ -z "$python_for_db" ]; then
  echo "check-tidy: no working python, and reading the compile database" >&2
  echo "  needs one. It is the same python that installs clang-tidy:" >&2
  echo "  pip install clang-tidy==$(cat .llvm-version)" >&2
  exit 127
fi

# A build tree only holds the targets that build for its toolchain: a
# native database has no entry for the wasm host, and a wasm one has none
# for the SDL host. clang-tidy would fall back to a guessed command line
# for those and report whatever that produced, so drop them here instead —
# and say which, because a file that quietly went unanalysed is the exact
# thing this gate exists to prevent.
#
# Which means the coverage question — does this database hold an entry for
# this source — has to be answered about the *file*, not about a spelling
# of its path. Two tools spell the same path differently and both are
# right: git says "C:/Dev/amberfolio", CMake's Ninja+MSVC generator has
# said "C:\Dev\amberfolio" and says "C:/Dev/amberfolio" today, and the
# format lets an entry give `file` relative to its own `directory`. So
# both sides are normalised to one spelling and compared, instead of one
# spelling being matched literally.
#
# git's spelling of the root, not pwd's: pwd under Git Bash would say
# "/c/Dev/amberfolio", which is not a path any other tool here writes and
# no amount of normalising makes it one.
root=$(git rev-parse --show-toplevel)

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
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/one-config"
printf '%s\n' "${files[@]}" > "$work/tracked"

"$python_for_db" - \
  "$build/compile_commands.json" \
  "$work" \
  "$root" <<'PYTHON'
import json
import os
import posixpath
import sys

database, work, root = sys.argv[1], sys.argv[2], sys.argv[3]


def absolute(path):
    """Drive-absolute, UNC, or POSIX-absolute — on any host, because this
    reads paths written by a toolchain that need not be this one."""
    return path.startswith("/") or (len(path) > 1 and path[1] == ":")


def key(path, base):
    """One spelling per file, so two tools' spellings of the same path
    compare equal: separators folded, a relative `file` resolved against
    its entry's `directory`, and `.`/`..` segments collapsed.

    A backslash is read as a separator, never as a filename character.
    POSIX would allow the latter, but nothing in this tree writes such a
    name, and both sides get the same treatment, so even a file called
    `a\\b.cpp` would still match itself.

    Case is left to os.path.normcase, which is the platform's own answer
    and the right one twice over: on Windows it folds case — the drive
    letter's included, so `C:` and `c:` agree — as that filesystem does,
    and on POSIX it changes nothing, where `Foo.cpp` and `foo.cpp` are
    two files and calling one covered because the other is would be
    exactly the quiet non-analysis this gate exists to prevent."""
    text = path.replace("\\", "/")
    if not absolute(text):
        text = base.replace("\\", "/").rstrip("/") + "/" + text
    return os.path.normcase(posixpath.normpath(text))


try:
    with open(database, encoding="utf-8") as handle:
        entries = json.load(handle)
except (OSError, ValueError) as problem:
    sys.exit("check-tidy: cannot read {}: {}".format(database, problem))


def rank(entry):
    """Debug first. Anything else, and any tree that names no
    configuration at all, ranks the same and keeps whichever came first."""
    return 0 if "/Debug/" in entry.get("output", "").replace("\\", "/") else 1


kept = {}
for entry in entries:
    name = entry.get("file")
    if name is None:
        continue
    where = key(name, entry.get("directory", ""))
    if where not in kept or rank(entry) < rank(kept[where]):
        kept[where] = entry

with open(os.path.join(work, "tracked"), encoding="utf-8") as handle:
    tracked = [line for line in handle.read().splitlines() if line]

covered = [name for name in tracked if key(name, root) in kept]
skipped = [name for name in tracked if key(name, root) not in kept]

for what, names in (("covered", covered), ("skipped", skipped)):
    with open(os.path.join(work, what), "w", encoding="utf-8") as handle:
        handle.write("".join(name + "\n" for name in names))

with open(os.path.join(work, "one-config", "compile_commands.json"),
          "w", encoding="utf-8") as handle:
    json.dump(list(kept.values()), handle)
PYTHON

mapfile -t covered < "$work/covered"
mapfile -t skipped < "$work/skipped"

if [ "${#skipped[@]}" -gt 0 ]; then
  echo "check-tidy: not in this build's compile database, skipping:"
  printf '  %s\n' "${skipped[@]}"
  echo "  (run again against a build tree that compiles them)"
fi

if [ "${#covered[@]}" -eq 0 ]; then
  echo "check-tidy: the compile database covers none of the sources" >&2
  exit 1
fi

"$CLANG_TIDY" -p "$work/one-config" --quiet "${covered[@]}"
echo "check-tidy: OK (${#covered[@]} files, clang-tidy $got, db $build)"
