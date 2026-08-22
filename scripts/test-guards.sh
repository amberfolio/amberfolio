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

# check-tidy.sh is not self-tested here: it needs a configured CMake build
# tree, which is minutes of SDL3 and GoogleTest, not a throwaway repo. The
# tidy CI job runs it against the real one.

if [ "$fail" -eq 0 ]; then
  echo "test-guards: OK"
fi
exit "$fail"
