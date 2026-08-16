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
  cp "$here/check-clean.sh" "$here/check-dco.sh" "$d/scripts/"
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

if [ "$fail" -eq 0 ]; then
  echo "test-guards: OK"
fi
exit "$fail"
