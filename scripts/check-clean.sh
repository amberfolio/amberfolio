#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Content guard: this repository must never contain material from the
# original games — no game code, data, or assets, in any form. Scans
# every (path, content) pair any commit reachable from HEAD ever
# recorded, the staged index, the tracked working tree and whatever is
# lying untracked beside it; runs in CI on every push (with full history
# fetched). This is a tripwire against obvious artifacts — denylisted
# filenames, oversized blobs, and anything that is not text and not on
# the allowlist below — while the deeper clean-content claim is made
# inspectable by the full public history, not proven by this script.
#
# Needs `python3`, for the one job shell cannot do a blob at a time
# without costing more than everything else here together: see
# `prime_verdicts`. CI's guards job installs it for the other gates
# already, and CLAUDE.md's command list assumes it.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0

# Game-artifact filenames must never appear.
# (These filenames are publicly documented; the list reveals nothing.)
DENY='(^|/)(start[^/]*\.exe|game\.ovr|pool\.cfg)$|\.(dax|sav|itm|spc)$'
# No large blobs (256 KiB cap until a documented exception exists).
# Original binaries and data files have no business here at any size;
# the cap catches them and anything else that should be questioned.
MAX=262144

# Nothing that is not text may be committed unless its path is named
# here. A denylist can only refuse names somebody thought of in advance,
# and #134 was 4.6 KiB of the program's own relocated overlay code under
# an invented name: under the cap, unlike any listed name, and waved
# through. Knowing every good binary is a list this repository can
# actually keep — it has one entry — and it does not depend on guessing
# what the next artifact will be called.
#
#   tests/sessions/spin/SPIN.EXE
#       34 bytes written here by hand: an MZ header and `JMP $`, and
#       nothing else. It is the program `tests/sessions/spin.rec`
#       replays, and the recording pins it by SHA-256, so it has to be
#       committed as the bytes it is rather than as a source file.
#
# Adding a line is a content decision, not a formatting one: say in one
# line where the bytes came from and why they are not game material.
ALLOW='^tests/sessions/spin/SPIN\.EXE$'

# Binary means what git means by it: a NUL byte in the first 8000 bytes.
# The verdict is memoised by object id, because the history walk sees
# the same blob once per commit that did not touch it — reading each
# one 200-odd times would cost more than everything else here together.
declare -A binary_by_oid
WINDOW=8000

# Answers in shell truth: success (0) means the content is binary.
is_binary() { # $1 = size, $2 = object id, or "-" to read $3 off disk
  local size="$1" oid="$2" path="$3" window kept verdict
  if [ "$oid" != "-" ] && [ -n "${binary_by_oid[$oid]-}" ]; then
    return "${binary_by_oid[$oid]}"
  fi
  window=$WINDOW
  if [ "$size" -lt "$window" ]; then
    window=$size
  fi
  if [ "$oid" = "-" ]; then
    kept=$(head -c "$window" <"$path" | LC_ALL=C tr -d '\000' | wc -c)
  else
    # The trailing `cat` throws away the rest of the blob rather than
    # letting `head` close the pipe under git: git would die of EPIPE,
    # print about it, and hand pipefail a nonzero status to abort on.
    # Blobs here are capped at 256 KiB, so draining one is cheap.
    kept=$(git cat-file blob "$oid" | { head -c "$window"; cat >/dev/null; } |
      LC_ALL=C tr -d '\000' | wc -c)
  fi
  verdict=1
  if [ "$kept" -ne "$window" ]; then
    verdict=0
  fi
  if [ "$oid" != "-" ]; then
    binary_by_oid[$oid]=$verdict
  fi
  return "$verdict"
}

# The binary verdict for every blob these two passes will ask about, in
# one `git cat-file --batch` rather than four processes a blob.
#
#    `is_binary` reads a blob by spawning git, `head`, `cat`, `tr` and
#    `wc`; at about a quarter-second a blob on this platform, the 2,472
#    of them were ten minutes on their own. Batching is the same rule
#    over the same bytes -- a NUL in the first 8,000 -- decided in one
#    pass over one stream.
#
#    Rejected: `git diff --numstat`, which marks a binary file `-` and
#    would be free. It is git's verdict *after* `.gitattributes`, and
#    this repository has one; a guard whose answer a committed file can
#    change is not a guard.
prime_verdicts() { # stdin = object ids, one a line
  local oid verdict
  # The single quotes around the python are the point: it is source,
  # not a string for the shell to expand.
  # shellcheck disable=SC2016
  while IFS=$'\t' read -r oid verdict; do
    binary_by_oid[$oid]=$verdict
  done < <(LC_ALL=C sort -u | git cat-file --batch | python3 -c '
import sys

WINDOW = 8000
stream = sys.stdin.buffer
out = []
while True:
    header = stream.readline()
    if not header:
        break
    fields = header.split()
    if len(fields) < 3:
        continue  # "<oid> missing"; the scan reads it the slow way
    name, kind, size = fields[0].decode(), fields[1].decode(), int(fields[2])
    head = stream.read(min(size, WINDOW))
    left = size - len(head)
    while left > 0:
        chunk = stream.read(min(left, 1 << 16))
        if not chunk:
            break
        left -= len(chunk)
    stream.read(1)  # the newline git writes after an object
    if kind == "blob":
        # Shell truth, the way is_binary answers it: 0 means binary.
        out.append("%s\t%d" % (name, 0 if b"\x00" in head else 1))
# Through the buffer rather than print(): on Windows a text stdout ends
# every line CRLF, and the read below then hands the shell a verdict of
# "1\r", which `return` refuses as not a number.
sys.stdout.buffer.write("".join(line + "\n" for line in out).encode())
')
}

scan() { # $1 = label; stdin = "size<TAB>oid<TAB>path[<TAB>where]" lines
  # `where` is optional and names the commit a history row came from, so
  # a failure still says which one rather than only which path. The
  # index and worktree passes leave it off and fall back to the label.
  local label="$1" size oid path where at
  while IFS=$'\t' read -r size oid path where; do
    [ -n "$path" ] || continue
    at=${where:-$label}
    # A bash match rather than a `grep` per file: history is thousands
    # of rows, and a process each was most of this script's runtime.
    # DENY is written lowercase throughout, so lowercasing the path is
    # what `grep -i` was doing.
    if [[ ${path,,} =~ $DENY ]]; then
      printf 'FAIL[%s]: game-artifact filename: %s\n' "$at" "$path"
      fail=1
    fi
    # A gitlink has no size and no blob; it is not a file to read.
    [ "$size" != "-" ] || continue
    if [ "$size" -gt "$MAX" ]; then
      printf 'FAIL[%s]: oversized file %s (%s bytes)\n' "$at" "$path" "$size"
      fail=1
    fi
    if ! [[ $path =~ $ALLOW ]] && is_binary "$size" "$oid" "$path"; then
      printf 'FAIL[%s]: not text and not on the allowlist: %s (%s bytes)\n' \
        "$at" "$path" "$size"
      fail=1
    fi
  done
}

# scan must NOT sit on the right side of a pipeline: bash would run it
# in a subshell and its fail=1 would be lost. Process substitution keeps
# it in the parent shell.

# 1) Every (path, content) pair any commit ever recorded.
#
#    This used to be `ls-tree -r` per commit, which is the same answer
#    with the redundancy left in: a file nobody touched is listed again
#    by every commit after the one that added it. On this history that
#    is 139,994 rows where there are 2,472 distinct pairs, and the loop
#    in `scan` is where the time went -- the git plumbing for the whole
#    walk is sixteen seconds, the loop was half an hour.
#
#    The coverage is identical, and was checked rather than argued: the
#    union of every commit's tree and the set below came out the same
#    2,472 pairs, neither holding one the other did not. `--raw` reports
#    every add and every modify, and `-m` makes it report them on merge
#    commits too, so a (path, content) pair cannot reach a tree without
#    appearing here first. Deletions, and the all-zero destination they
#    carry, are dropped: there is nothing left to read.
#
#    Rejected: re-verifying only a window of recent history, and
#    verifying only what is new since a mark. A window trusts commit
#    dates, which are whatever the committer set them to. A mark would
#    be sound -- a commit's hash covers its whole ancestry, so
#    `merge-base --is-ancestor` proves the range under it is the range
#    that was verified -- but it buys nothing once the redundancy is
#    gone, and it would have CI trust a file this repository does not
#    carry.
history_rows=$(
  declare -A size_by_oid type_by_oid
  pairs=$(git -c core.quotePath=false log -m --pretty=format:%H --raw \
            --no-renames --abbrev=40 HEAD |
    awk -F'\t' '
      /^[0-9a-f]{40}$/ { commit = substr($0, 1, 7); next }
      NF == 2 {
        split($1, f, " ")
        if (f[5] != "D" && f[4] !~ /^0+$/) { print f[4] "\t" $2 "\t" commit }
      }' |
    LC_ALL=C sort -t$'\t' -u -k1,2)
  if [ -n "$pairs" ]; then
    while read -r oid kind size; do
      type_by_oid[$oid]=$kind
      size_by_oid[$oid]=$size
    done < <(printf '%s\n' "$pairs" | cut -f1 | LC_ALL=C sort -u |
      git cat-file --batch-check='%(objectname) %(objecttype) %(objectsize)')
    while IFS=$'\t' read -r oid path commit; do
      # A gitlink names a commit rather than a blob: no size, and
      # nothing to read, which is the `-` the scan skips on.
      if [ "${type_by_oid[$oid]-}" = blob ]; then
        printf '%s\t%s\t%s\t%s\n' \
          "${size_by_oid[$oid]}" "$oid" "$path" "$commit"
      else
        printf -- '-\t%s\t%s\t%s\n' "$oid" "$path" "$commit"
      fi
    done <<<"$pairs"
  fi
)

# 2) The staged index — what a commit would actually record. The
#    working tree alone is not enough: a staged blob survives replacing
#    or deleting the file on disk. Sizes come from one `--batch-check`
#    rather than a `git cat-file -s` a file, for the reason above.
index_rows=$(
  staged=$(git -c core.quotePath=false ls-files -s |
    awk -F'\t' '{ split($1, f, " "); print f[2] "\t" $2 }')
  if [ -n "$staged" ]; then
    awk -F'\t' 'NR == FNR { size[$1] = $2; next }
                { print size[$1] "\t" $1 "\t" $2 }' \
      <(printf '%s\n' "$staged" | cut -f1 | LC_ALL=C sort -u |
        git cat-file --batch-check='%(objectname) %(objectsize)' |
        tr ' ' '\t') \
      <(printf '%s\n' "$staged")
  fi
)

# One batch for both, then the scans, which only look a verdict up.
prime_verdicts < <(printf '%s\n%s\n' "$history_rows" "$index_rows" |
  cut -f2 | grep -v '^-$')
scan history <<<"$history_rows"
scan index <<<"$index_rows"

# 3) The working tree (tracked files), for local pre-commit use. What is
#    on disk has no object id yet, so its content is read from the path.
scan worktree < <(
  while IFS= read -r f; do
    [ -f "$f" ] || continue
    printf '%s\t-\t%s\n' "$(wc -c < "$f")" "$f"
  done < <(git ls-files)
)

# 4) Untracked, non-ignored files. The three passes above see only what
#    git already knows about, and #134 arrived as a stray that `git add
#    -A` swept up — at the one moment it was still free to fix, it was
#    invisible to all of them. Only the binary test applies here: a
#    working tree may hold anything a person is in the middle of, but a
#    file that is not text, not ignored and not yet tracked is one
#    careless `add` away from being history. CI checks out clean, so
#    this pass costs nothing there.
while IFS= read -r f; do
  [ -f "$f" ] || continue
  if [[ $f =~ $ALLOW ]]; then
    continue
  fi
  size=$(wc -c < "$f")
  if is_binary "$size" "-" "$f"; then
    printf 'FAIL[untracked]: not text, and one git add away from history: %s (%s bytes)\n' \
      "$f" "$size"
    printf '  Scratch output belongs outside the repository, or in .gitignore;\n'
    printf '  anything meant to be committed needs a line in this guard first.\n'
    fail=1
  fi
done < <(git ls-files --others --exclude-standard)

if [ "$fail" -eq 0 ]; then
  echo "check-clean: OK ($(git rev-list --count HEAD) commits + index + worktree)"
fi
exit "$fail"
