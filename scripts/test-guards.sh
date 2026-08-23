#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Self-test for the guard scripts: builds throwaway repositories and
# asserts each guard FAILS on the violations it exists to catch.
# Green-path runs alone cannot prove enforcement — a subshell bug once
# swallowed the fail flag while still printing FAIL lines.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0

expect() { # expect <description> <wanted-exit: 0|nonzero> <command...>
  local desc="$1" want="$2"; shift 2
  local got=0; "$@" >/dev/null 2>&1 || got=$?
  if { [ "$want" = 0 ] && [ "$got" -eq 0 ]; } \
     || { [ "$want" != 0 ] && [ "$got" -ne 0 ]; }; then
    echo "ok: $desc"
  else
    echo "FAIL: $desc (wanted exit $want, got $got)"
    fail=1
  fi
}

mkrepo() { # mkrepo <name> -> prints repo path
  local d="$tmp/$1"
  mkdir -p "$d/scripts"
  cp "$here"/check-*.sh "$d/scripts/"
  # The gates that read configuration need it; the ones that do not are
  # unaffected by the extra files.
  cp "$here/../.clang-format" "$here/../.llvm-version" "$d/"
  git -C "$d" init -q -b main
  git -C "$d" config user.email test@example.com
  git -C "$d" config user.name "Guard Test"
  git -C "$d" config core.autocrlf false
  echo base > "$d/base.txt"
  git -C "$d" add -A
  git -C "$d" commit -q -s -m base
  echo "$d"
}

r=$(mkrepo clean)
expect "clean repo passes content guard" 0 bash "$r/scripts/check-clean.sh"
expect "clean repo passes dco check"     0 bash "$r/scripts/check-dco.sh"

r=$(mkrepo denyname)
echo junk > "$r/START_TEST.EXE"
git -C "$r" add -A
git -C "$r" commit -q -m "bad commit"
expect "denylisted filename in a commit fails" 1 bash "$r/scripts/check-clean.sh"
expect "unsigned commit fails dco check"       1 bash "$r/scripts/check-dco.sh"
git -C "$r" rm -q START_TEST.EXE
git -C "$r" commit -q -s -m "remove bad file"
expect "violation buried in history still fails" 1 bash "$r/scripts/check-clean.sh"

r=$(mkrepo bigfile)
head -c 300000 /dev/zero > "$r/big.bin"
git -C "$r" add big.bin
expect "oversized staged file fails" 1 bash "$r/scripts/check-clean.sh"
rm "$r/big.bin"
expect "staged blob with working copy deleted still fails" 1 \
  bash "$r/scripts/check-clean.sh"

# The binary allowlist (#134). The dump that got through was 4.6 KiB
# under an invented name: below the cap, on no denylist. What is not
# text now has to be named in the guard before it can be committed.
r=$(mkrepo binary)
printf 'MZ\000\000dump\n' > "$r/dump.bin"
expect "an untracked binary is caught before it is ever staged" 1 \
  bash "$r/scripts/check-clean.sh"
git -C "$r" add dump.bin
expect "a staged binary that is not on the allowlist fails" 1 \
  bash "$r/scripts/check-clean.sh"
git -C "$r" commit -q -s -m "dump"
expect "a committed binary that is not on the allowlist fails" 1 \
  bash "$r/scripts/check-clean.sh"
git -C "$r" rm -q dump.bin
git -C "$r" commit -q -s -m "remove dump"
expect "a binary buried in history still fails" 1 \
  bash "$r/scripts/check-clean.sh"

# The allowlist is a path list, not a rule about content: the one entry
# passes staged and committed, and nothing else moved into its place
# does. Binary is decided the way git decides it — a NUL in the first
# 8000 bytes — so a NUL past that window is not this gate's business.
r=$(mkrepo allowlisted)
mkdir -p "$r/tests/sessions/spin"
printf 'MZ\000\000spin\n' > "$r/tests/sessions/spin/SPIN.EXE"
git -C "$r" add tests/sessions/spin/SPIN.EXE
expect "the allowlisted binary passes staged" 0 bash "$r/scripts/check-clean.sh"
git -C "$r" commit -q -s -m "spin"
expect "the allowlisted binary passes committed" 0 bash "$r/scripts/check-clean.sh"
printf 'MZ\000\000spin\n' > "$r/tests/sessions/spin/OTHER.EXE"
git -C "$r" add tests/sessions/spin/OTHER.EXE
expect "a sibling of the allowlisted binary is not covered by it" 1 \
  bash "$r/scripts/check-clean.sh"
git -C "$r" rm -q --cached tests/sessions/spin/OTHER.EXE
rm "$r/tests/sessions/spin/OTHER.EXE"
{ head -c 9000 /dev/zero | tr '\000' 'a'; printf '\000\n'; } > "$r/late.txt"
git -C "$r" add late.txt
expect "a NUL past the first 8000 bytes is still text" 0 \
  bash "$r/scripts/check-clean.sh"
git -C "$r" rm -q --cached late.txt
rm "$r/late.txt"
printf 'plain\n' > "$r/note.txt"
expect "an untracked text file is nobody's violation" 0 \
  bash "$r/scripts/check-clean.sh"
printf 'MZ\000\000dump\n' > "$r/scratch.bin"
expect "an untracked binary beside it fails" 1 \
  bash "$r/scripts/check-clean.sh"
printf '*.bin\n' > "$r/.gitignore"
expect "an ignored binary is out of the untracked pass" 0 \
  bash "$r/scripts/check-clean.sh"

r=$(mkrepo badsign)
echo x > "$r/f.txt"
git -C "$r" add f.txt
git -C "$r" commit -q -m $'ok commit\n\nSigned-off-by: @'
expect "degenerate sign-off fails dco check" 1 bash "$r/scripts/check-dco.sh"

r=$(mkrepo mergeok)
git -C "$r" checkout -q -b side
echo y > "$r/s.txt"
git -C "$r" add s.txt
git -C "$r" commit -q -s -m "side change"
git -C "$r" checkout -q main
git -C "$r" merge -q --no-ff --no-edit side
expect "unsigned merge commit is exempt from dco" 0 bash "$r/scripts/check-dco.sh"

# The host-time guard (#78): a clock read under core/ fails, the same
# text anywhere else does not, and the names core legitimately uses —
# `machine::time()`, `wall_time` — are not caught.
r=$(mkrepo hosttime)
mkdir -p "$r/core/src" "$r/hosts"
printf '#include <cstdint>\nstd::uint64_t now() { return 0; }\n' > "$r/core/src/clean.cpp"
printf 'struct m { std::uint64_t time() const; }; void f(m& box) { (void)box.time(); }\n' \
  > "$r/core/src/legal.cpp"
git -C "$r" add -A
expect "core without a clock read passes host-time guard" 0 \
  bash "$r/scripts/check-host-time.sh"
printf '#include <chrono>\n' > "$r/core/src/chrono.cpp"
git -C "$r" add -A
expect "a <chrono> include under core fails host-time guard" 1 \
  bash "$r/scripts/check-host-time.sh"
git -C "$r" rm -q --cached core/src/chrono.cpp
rm "$r/core/src/chrono.cpp"
printf '#include <ctime>\nlong n() { return (long)time(nullptr); }\n' > "$r/core/src/libc.cpp"
git -C "$r" add -A
expect "a libc time() call under core fails host-time guard" 1 \
  bash "$r/scripts/check-host-time.sh"
git -C "$r" rm -q --cached core/src/libc.cpp
rm "$r/core/src/libc.cpp"
printf '#include <chrono>\n' > "$r/hosts/clock.cpp"
git -C "$r" add -A
expect "a clock read outside core is not the host-time guard's business" 0 \
  bash "$r/scripts/check-host-time.sh"

# The format and shell gates need their tools. Skipping is announced, not
# silent: a self-test that reports OK for a case it never ran is worse
# than one that does not run at all.
if command -v "${CLANG_FORMAT:-clang-format}" >/dev/null 2>&1; then
  r=$(mkrepo format)
  printf 'int formatted(int x) { return x; }\n' > "$r/ok.cpp"
  git -C "$r" add -A
  expect "formatted source passes format gate" 0 \
    bash "$r/scripts/check-format.sh"
  printf 'int   bad ( int  x ){return x ;}\n' > "$r/bad.cpp"
  git -C "$r" add -A
  expect "unformatted source fails format gate" 1 \
    bash "$r/scripts/check-format.sh"
  # Untracked is not exempt-by-accident: the gate walks git ls-files, so a
  # file has to be staged to be seen. Pin that, or a reviewer could read
  # the gate as covering the working tree.
  git -C "$r" rm -q --cached bad.cpp
  expect "unstaged file is outside the format gate" 0 \
    bash "$r/scripts/check-format.sh"
else
  echo "skip: clang-format not installed (format gate not self-tested)"
fi

if command -v "${SHELLCHECK:-shellcheck}" >/dev/null 2>&1; then
  r=$(mkrepo shell)
  expect "the guards' own scripts pass shellcheck" 0 \
    bash "$r/scripts/check-shell.sh"
  # Literal, unexpanded: the whole point is that $f reaches the generated
  # script intact, so shellcheck has something to object to.
  # shellcheck disable=SC2016
  printf '#!/usr/bin/env bash\nf=$1\nls $f\n' > "$r/scripts/unquoted.sh"
  git -C "$r" add -A
  expect "unquoted expansion fails the shell gate" 1 \
    bash "$r/scripts/check-shell.sh"
else
  echo "skip: shellcheck not installed (shell gate not self-tested)"
fi

# check-tidy.sh's coverage step (#39): which tracked sources a build
# tree's compile database holds an entry for. How a path is spelled in
# that database is a generator's choice — CMake's Ninja+MSVC generator
# has written both separators across releases, and the format lets an
# entry give `file` relative to its `directory` — and matching one
# spelling literally is what left Windows without this gate for two
# milestones. Nothing announced that spelling when it moved, so pin every
# spelling here instead.
#
# The analysis itself is not what is under test: a real clang-tidy run
# needs a configured CMake tree, which is minutes of SDL3 and GoogleTest
# rather than a throwaway repo, and the tidy CI job does it against the
# real one. So clang-tidy is stubbed and only the arithmetic runs.
tidy_python=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1 &&
     "$candidate" -c "" >/dev/null 2>&1; then
    tidy_python="$candidate"
    break
  fi
done

if [ -n "$tidy_python" ]; then
  r=$(mkrepo tidy)
  printf 'int covered(int x) { return x; }\n' > "$r/covered.cpp"
  printf 'int elsewhere(int x) { return x; }\n' > "$r/elsewhere.cpp"
  git -C "$r" add covered.cpp elsewhere.cpp
  git -C "$r" commit -q -s -m "sources"

  mkdir -p "$r/stub"
  {
    printf '#!/usr/bin/env bash\n'
    # Literal, unexpanded: $1 belongs to the generated stub, which has to
    # answer the gate's version check and nothing else.
    # shellcheck disable=SC2016
    printf 'if [ "${1:-}" = --version ]; then echo "LLVM version %s"; fi\n' \
      "$(cat "$here/../.llvm-version")"
    printf 'exit 0\n'
  } > "$r/stub/clang-tidy"
  chmod +x "$r/stub/clang-tidy"

  # git's spelling of the root, because that is the one the gate compares
  # against — and the same root respelled the way a Windows generator
  # writes it, each separator doubled because that is how a backslash
  # reaches a JSON string.
  slash=$(git -C "$r" rev-parse --show-toplevel)
  back=$(printf '%s' "$slash" | sed 's|/|\\\\|g')

  tidy_db() { # tidy_db <build-dir> <entries-json>
    mkdir -p "$r/$1"
    printf '[%s]\n' "$2" > "$r/$1/compile_commands.json"
  }
  # Both of these run through `expect`, which invokes what it is handed.
  # Two codes for one finding: shellcheck spells "called indirectly" as
  # SC2329 in 0.11 and as SC2317 in the version CI installs, and an
  # unknown code in a disable directive is ignored rather than an error,
  # so naming both is what keeps the gate green on either.
  # shellcheck disable=SC2329,SC2317
  tidy_run() { # tidy_run <build-dir>
    env CLANG_TIDY="$r/stub/clang-tidy" bash "$r/scripts/check-tidy.sh" "$1"
  }
  # shellcheck disable=SC2329,SC2317
  tidy_says() { # tidy_says <build-dir> <text>
    local said
    said=$(tidy_run "$1" 2>&1) || true
    case $said in *"$2"*) return 0 ;; *) return 1 ;; esac
  }

  tidy_db build/forward "$(printf \
    '{"directory": "%s/build/forward", "command": "c++ -c x", "file": "%s/covered.cpp"},
     {"directory": "%s/build/forward", "command": "c++ -c x", "file": "%s/elsewhere.cpp"}' \
    "$slash" "$slash" "$slash" "$slash")"
  expect "a forward-slash database covers the sources" 0 tidy_run build/forward

  tidy_db build/backslash "$(printf \
    '{"directory": "%s\\\\build\\\\backslash", "command": "cl x", "file": "%s\\\\covered.cpp"},
     {"directory": "%s\\\\build\\\\backslash", "command": "cl x", "file": "%s\\\\elsewhere.cpp"}' \
    "$back" "$back" "$back" "$back")"
  expect "a backslash-spelled database covers the same sources" 0 \
    tidy_run build/backslash

  # `file` relative to `directory`, with `.`/`..` to fold away: the JSON
  # format allows it and generators do it.
  tidy_db build/relative "$(printf \
    '{"directory": "%s/build/relative", "command": "c++ -c x", "file": "./../../covered.cpp"},
     {"directory": "%s/build/relative", "command": "c++ -c x", "file": "../../elsewhere.cpp"}' \
    "$slash" "$slash")"
  expect "a database with relative file fields covers the same sources" 0 \
    tidy_run build/relative

  # The reason coverage is computed at all: a source a build tree does not
  # compile is named, not passed over quietly.
  tidy_db build/partial "$(printf \
    '{"directory": "%s/build/partial", "command": "c++ -c x", "file": "%s/covered.cpp"}' \
    "$slash" "$slash")"
  expect "a partly covering database still analyses what it covers" 0 \
    tidy_run build/partial
  expect "a source the database omits is named, not silently dropped" 0 \
    tidy_says build/partial elsewhere.cpp

  tidy_db build/foreign "$(printf \
    '{"directory": "%s/build/foreign", "command": "c++ -c x", "file": "%s/other/thing.cpp"}' \
    "$slash" "$slash")"
  expect "a database covering none of the sources fails" 1 tidy_run build/foreign
else
  echo "skip: no working python (tidy coverage not self-tested)"
fi

if [ "$fail" -eq 0 ]; then
  echo "test-guards: OK"
fi
exit "$fail"
