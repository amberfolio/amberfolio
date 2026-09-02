#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Self-test for scripts/visual-legs.py's bookkeeping (#234): what the
# runner decides about a leg *before* a machine is asked to run one, and
# what it says when it could not run anything at all.
#
# Why this exists, for the fourth time in this tree. A visual leg has the
# same three outcomes a session has — ok, FAIL, and skipped-because-this
# machine has no disk — and the same rule about the third: a run that
# checked nothing must never read as a run that passed. That is a
# property of an output, and an output nobody asserts is an output that
# drifts.
#
# The green path — a leg actually driving the program and holding — needs
# the player's own disk and cannot run in CI. `python3
# scripts/visual-legs.py --game-disk <a copy>` is that check, and
# `docs/journal-test-plan.md` §9 says how to get to it.
#
# The legs built below are inventions: made-up keys over a made-up
# program. Nothing here is, or resembles, any game artefact.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
python=${PYTHON:-python3}

out=""
code=0
legs() { # legs <repo> [args...]
  code=0
  out=$(cd "$1" && "$python" "$1/scripts/visual-legs.py" "${@:2}" 2>&1) || code=$?
}

expect_code() { # expect_code <description> <wanted>
  if [ "$code" -eq "$2" ]; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (wanted exit $2, got $code)"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  fi
}

expect_says() { # expect_says <description> <text>
  if printf '%s' "$out" | grep -qF -- "$2"; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (nothing said '$2')"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  fi
}

# A throwaway repository: the two scripts, and a legs directory to fill.
repo="$tmp/repo"
mkdir -p "$repo/scripts" "$repo/tests/visual"
cp "$here/visual-legs.py" "$here/frames.py" "$repo/scripts/"

# A stub where the desktop host would be, so that the cases below reach
# the checks they are about rather than stopping at "this tree has no
# host". Nothing ever executes it: every leg here is refused or skipped
# before a run starts, which is itself one of the things asserted.
mkdir -p "$repo/build/windows-msvc/hosts/sdl"
: > "$repo/build/windows-msvc/hosts/sdl/amberfolio"

leg() { # leg <name> <body...>
  local name="$1"; shift
  printf '%s\n' "$@" > "$repo/tests/visual/$name.leg"
}

# --- a leg that is complete, with no disk to run it over ------------------
leg good \
  "about a leg that would run" \
  "kind pair" \
  "program MADEUP.EXE" \
  "until 1000" \
  "key A@10" \
  "rect panel 136,8,311,119" \
  "allow 0-end panel"

legs "$repo"
expect_code "with no disk, nothing ran and that is not a pass" 1
expect_says "the leg is skipped" "SKIP  good"
expect_says "and it says what was missing" "no disk"
expect_says "and the run says so in as many words" "NOTHING WAS CHECKED"

# The same, with a directory that is not there: still a skip, never a pass.
legs "$repo" --game-disk "$tmp/no-such-disk"
expect_code "a disk that is not there is still a skip" 1
expect_says "and still says nothing was checked" "NOTHING WAS CHECKED"

# --- legs that check nothing are refused, not quietly passed --------------
#
# The failure this guards against is a leg file that parses, runs, and
# asserts nothing — which would read as a green line in the table.
mkdir -p "$tmp/disk"
: > "$tmp/disk/MADEUP.EXE"

leg empty-pair \
  "kind pair" \
  "program MADEUP.EXE" \
  "until 1000"
legs "$repo" --game-disk "$tmp/disk" --leg empty-pair
expect_code "a pair leg with no allow line is refused" 1
expect_says "and says why" "checks nothing"

leg empty-single \
  "kind single" \
  "program MADEUP.EXE" \
  "until 1000"
legs "$repo" --game-disk "$tmp/disk" --leg empty-single
expect_code "a single leg with no same line is refused" 1
expect_says "and says why" "checks nothing"

# --- a leg that is malformed says which line, and does not run -----------
leg no-kind \
  "program MADEUP.EXE" \
  "until 1000" \
  "allow 0-end none"
legs "$repo" --game-disk "$tmp/disk" --leg no-kind
expect_code "a leg with no kind is refused" 1
expect_says "and names the missing line" "kind pair"

leg typo \
  "kind pair" \
  "program MADEUP.EXE" \
  "until 1000" \
  "allow 0-end none" \
  "recct panel 1,2,3,4"
legs "$repo" --game-disk "$tmp/disk" --leg typo
expect_code "a keyword nobody knows is refused" 1
expect_says "and quotes the line" "line 5"

# A rect named by an allow line but never defined is the mistake a person
# actually makes, and it would otherwise be a crash halfway through a run.
leg unknown-rect \
  "kind pair" \
  "program MADEUP.EXE" \
  "until 1000" \
  "rect panel 136,8,311,119" \
  "allow 0-end pannel"
legs "$repo" --game-disk "$tmp/disk" --leg unknown-rect
expect_code "an allow naming an undefined rect is refused" 1
expect_says "and says which name" "pannel"

# --- and a store a leg names but nobody committed -------------------------
leg no-store \
  "kind pair" \
  "program MADEUP.EXE" \
  "store tests/visual/not-here.txt" \
  "until 1000" \
  "rect panel 136,8,311,119" \
  "allow 0-end panel"
legs "$repo" --game-disk "$tmp/disk" --leg no-store
expect_code "a leg whose store is missing is skipped" 1
expect_says "and says which store" "not-here.txt"

# --- the committed legs parse ---------------------------------------------
#
# Not run — that needs the disk — but every one of them is read, so a leg
# with a typo in it is caught here rather than on the one machine that
# can run it.
cp "$here"/../tests/visual/*.leg "$repo/tests/visual/" 2>/dev/null || true
rm -f "$repo/tests/visual/empty-pair.leg" "$repo/tests/visual/empty-single.leg" \
      "$repo/tests/visual/no-kind.leg" "$repo/tests/visual/typo.leg" \
      "$repo/tests/visual/unknown-rect.leg" "$repo/tests/visual/no-store.leg" \
      "$repo/tests/visual/good.leg"
mkdir -p "$repo/tests/visual"
cp "$here"/../tests/visual/reader-store.txt "$repo/tests/visual/"
legs "$repo"
expect_code "the committed legs are all skipped without a disk" 1
if printf '%s' "$out" | grep -qE '^(SKIP|ok|FAIL)' &&
   ! printf '%s' "$out" | grep -q "line "; then
  echo "ok: and none of them has a parse problem"
else
  echo "FAIL: a committed leg does not parse"
  printf '%s\n' "$out" | sed 's/^/    /'
  fail=1
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "test-visual-legs: OK"
else
  echo "test-visual-legs: FAILED"
fi
exit "$fail"
