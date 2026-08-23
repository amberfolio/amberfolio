#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Self-test for scripts/sweep.py's session bookkeeping (M4-R2, #101):
# builds throwaway session libraries and asserts what the runner decides
# about each one *before* a machine is asked to run anything.
#
# Why this exists at all. A session whose disk cannot be committed has a
# third outcome beside pass and fail — skipped, because the disk this
# machine has is not the disk the recording was made over — and the
# decision on #101 is explicit that the third must never read as the
# first. That is a property of an output, and an output nobody asserts is
# an output that drifts. So every case here checks the words as well as
# the exit status: a run that verified nothing has to say so, in capitals,
# and exit non-zero.
#
# The green path — a target actually reproducing a recording — is not
# here, because it needs a built host and a disk. `python3 scripts/
# sweep.py` over the committed library is that check, and CI runs the
# same sessions through the suites on every push.
#
# The sessions built below are inventions: a made-up recording of a
# made-up program over a made-up disk. Nothing here is, or resembles, any
# game artefact.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
python=${PYTHON:-python3}

# Every case runs sweep and keeps both halves of its answer: the exit
# status and what it said. A skip that exits zero and a skip that says
# nothing are each the bug this file exists to catch, and neither shows
# up in the other's assertion.
out=""
code=0
sweep() { # sweep <repo> [args...]
  local repo="$1"; shift
  code=0
  out=$("$python" "$repo/scripts/sweep.py" --targets sdl "$@" 2>&1) || code=$?
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

expect_silent() { # expect_silent <description> <text>
  if printf '%s' "$out" | grep -qF -- "$2"; then
    echo "FAIL: $1 (it said '$2')"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  else
    echo "ok: $1"
  fi
}

mkrepo() { # mkrepo <name> -> prints repo path
  local d="$tmp/$1"
  mkdir -p "$d/scripts" "$d/tests/sessions"
  cp "$here/sweep.py" "$d/scripts/"
  echo "$d"
}

# A recording of nothing, in the grammar sweep reads: the runner only
# looks for the program and the manifest, and never gets as far as
# running one in this file.
mkrec() { # mkrec <repo> <name>
  {
    echo "amberfolio-recording 1 state=1"
    echo "program MADEUP.EXE 00"
    echo "file MADEUP.EXE 4 00"
    echo "end 1 1"
  } > "$1/tests/sessions/$2.rec"
}

mkdisk() { # mkdisk <path>
  mkdir -p "$1/SAVE"
  printf 'made' > "$1/MADEUP.EXE"
  printf 'slot' > "$1/SAVE/SLOT.DAT"
}

# --- A recording nobody said anything about ----------------------------
#
# Not a skip. A `.rec` with no descriptor is a session that does not say
# which disk it wants, and the runner cannot tell "the maintainer's copy"
# from "a directory somebody forgot to commit" by looking. Guessing from
# whether a directory happens to exist is what would turn a missing
# commit into a quiet skip, which is the failure this whole shape is
# arranged against.
r=$(mkrepo orphan)
mkrec "$r" lonely
sweep "$r"
expect_code "a recording with no descriptor fails" 1
expect_says "and says what is missing" "no descriptor at"

# --- A descriptor that does not say which disk -------------------------
r=$(mkrepo nodisk)
mkrec "$r" quiet
echo "about a session that forgot the one line that matters" \
  > "$r/tests/sessions/quiet.session"
sweep "$r"
expect_code "a descriptor with no disk line fails" 1
expect_says "and says which line" "no disk line"

# --- Two pins that could disagree --------------------------------------
r=$(mkrepo doublepin)
mkrec "$r" both
mkdisk "$r/tests/sessions/both"
{
  echo "disk both"
  echo "file MADEUP.EXE 4 00"
} > "$r/tests/sessions/both.session"
sweep "$r"
expect_code "a committed disk with file lines fails" 1
expect_says "and says why one pin is enough" "pinned by git"

# --- The disk this machine does not have -------------------------------
#
# The case CI is in, and every second person's. It must not pass, it must
# not read as a pass, and it must name what it wanted.
r=$(mkrepo absent)
mkrec "$r" away
echo "disk external" > "$r/tests/sessions/away.session"
sweep "$r"
expect_code "an external session with no disk given does not pass" 1
expect_says "and is spelled as a skip" "SKIP"
expect_says "and says this disk is not that disk" "this disk is not that disk"
expect_says "and refuses to look like a pass" "NOTHING WAS VERIFIED"
expect_says "and names the session that was not checked" "NOT VERIFIED"

# --- A disk that is a different disk -----------------------------------
#
# Three ways for a directory to be the wrong one, and all three are
# skips rather than failures: a disk that is not the recorded one says
# nothing at all about the emulator, and reporting it as a divergence
# would be a finding about the machine that is really a finding about a
# directory.
r=$(mkrepo drifted)
mkrec "$r" leg
echo "disk external" > "$r/tests/sessions/leg.session"
mkdisk "$tmp/copy"
"$python" "$r/scripts/sweep.py" --pin leg --game-disk "$tmp/copy" >/dev/null
if grep -q '^file SAVE\\SLOT.DAT 4 ' "$r/tests/sessions/leg.session"; then
  echo "ok: --pin descends into the save directory"
else
  echo "FAIL: --pin did not pin the file in the save directory"
  fail=1
fi

printf 'later' > "$tmp/copy/SAVE/SLOT.DAT"
sweep "$r" --game-disk "$tmp/copy"
expect_code "a save one run further along does not pass" 1
expect_says "and is a skip, not a divergence" "this disk is not that disk"
expect_silent "and is not reported as a failure" "FAIL "

printf 'slot' > "$tmp/copy/SAVE/SLOT.DAT"
printf 'extra' > "$tmp/copy/EXTRA.DAT"
sweep "$r" --game-disk "$tmp/copy"
expect_code "a file the pin does not name does not pass" 1
expect_says "and says which one" "also holds EXTRA.DAT"

rm "$tmp/copy/EXTRA.DAT" "$tmp/copy/MADEUP.EXE"
sweep "$r" --game-disk "$tmp/copy"
expect_code "a file the pin names and the disk lacks does not pass" 1
expect_says "and says which one" "lacks MADEUP.EXE"

# --- And the pin accepting the disk it was taken from ------------------
#
# The one green thing this file can check without a build tree: the disk
# passes its comparison and the run gets as far as looking for a host.
# Asserted as the *absence* of the skip above, because a check that only
# ever says no is a check that would pass if it always said no.
printf 'made' > "$tmp/copy/MADEUP.EXE"
sweep "$r" --game-disk "$tmp/copy"
expect_silent "the disk it was pinned from is that disk" \
  "this disk is not that disk"
expect_says "and the run went looking for a host" "no desktop host"

# --- A pair that is supposed to differ ---------------------------------
#
# Two recordings of the same script one flag apart, and the assertion
# that the flag mattered. This is the check `docs/seams.md` asks for
# after a seam was twice on, armed, reporting itself, and doing nothing —
# with a green suite throughout. It compares files rather than machines,
# which is what lets CI make it about sessions no runner here can replay.

mkpair() { # mkpair <repo> <name> <partner> <hashes...>
  local repo="$1" name="$2" partner="$3"; shift 3
  {
    echo "amberfolio-recording 1 state=1"
    echo "program MADEUP.EXE 00"
    local tick=1000
    for h in "$@"; do
      echo "checkpoint $tick 1 $h"
      tick=$((tick + 1000))
    done
    echo "end $tick 1"
  } > "$repo/tests/sessions/$name.rec"
  {
    echo "disk external"
    if [ -n "$partner" ]; then echo "contrast $partner"; fi
  } > "$repo/tests/sessions/$name.session"
}

contrast() { # contrast <repo>
  code=0
  out=$("$python" "$1/scripts/sweep.py" --targets contrast 2>&1) || code=$?
}

r=$(mkrepo pair)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain aa bb ff ee
contrast "$r"
expect_code "a pair that shares a prefix and then differs passes" 0
expect_says "and says where it parted" "2 of 4 checkpoints identical"

r=$(mkrepo inert)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain aa bb cc dd
contrast "$r"
expect_code "a pair that is identical throughout fails" 1
expect_says "and says the difference made none" "made no difference"

r=$(mkrepo late)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain aa bb cc ee
contrast "$r"
expect_code "a pair that differs only at the end still passes" 0

r=$(mkrepo rejoined)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain aa ff cc dd
contrast "$r"
expect_code "a pair that diverges and comes back together fails" 1
expect_says "and says the difference did not last" "did not last"

r=$(mkrepo fromthetop)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain ee ff gg hh
contrast "$r"
expect_code "a pair that never agreed at all fails" 1
expect_says "and says a pair should share the run first" "should share the run"

r=$(mkrepo shifted)
mkpair "$r" plain "" aa bb cc dd
mkpair "$r" seamed plain aa bb cc
contrast "$r"
expect_code "a pair checkpointing at different ticks fails" 1
expect_says "and says they are not the same run" "not the same run"

r=$(mkrepo orphanpair)
mkpair "$r" seamed nobody aa bb cc
contrast "$r"
expect_code "a contrast naming no session fails" 1
expect_says "and says which name" "no session called nobody"

# --- And the committed pair, in this tree ------------------------------
#
# The one case here that is about the real session library rather than an
# invention. It needs no disk and no build tree, so CI runs it: if a
# recorded seam ever stops making a difference, this is where it is said.
contrast "$here/.."
expect_code "the committed sessions' contrasts hold" 0

if [ "$fail" -ne 0 ]; then
  echo "sweep self-test: FAILED"
  exit 1
fi
echo "sweep self-test: all cases behaved"
