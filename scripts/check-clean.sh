#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Content guard: this repository must never contain material from the
# original games — no game code, data, or assets, in any form. Scans
# EVERY commit reachable from HEAD, the staged index, the tracked
# working tree and whatever is lying untracked beside it; runs in CI on
# every push (with full history fetched). This is a tripwire against
# obvious artifacts — denylisted filenames, oversized blobs, and
# anything that is not text and not on the allowlist below — while the
# deeper clean-content claim is made inspectable by the full public
# history, not proven by this script.
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

scan() { # $1 = label; stdin = "size<TAB>object-id<TAB>path" lines
  local label="$1" size oid path
  while IFS=$'\t' read -r size oid path; do
    [ -n "$path" ] || continue
    # A bash match rather than a `grep` per file: history is tens of
    # thousands of tree entries, and a process each was most of this
    # script's runtime. DENY is written lowercase throughout, so
    # lowercasing the path is what `grep -i` was doing.
    if [[ ${path,,} =~ $DENY ]]; then
      printf 'FAIL[%s]: game-artifact filename: %s\n' "$label" "$path"
      fail=1
    fi
    # A gitlink has no size and no blob; it is not a file to read.
    [ "$size" != "-" ] || continue
    if [ "$size" -gt "$MAX" ]; then
      printf 'FAIL[%s]: oversized file %s (%s bytes)\n' "$label" "$path" "$size"
      fail=1
    fi
    if ! [[ $path =~ $ALLOW ]] && is_binary "$size" "$oid" "$path"; then
      printf 'FAIL[%s]: not text and not on the allowlist: %s (%s bytes)\n' \
        "$label" "$path" "$size"
      fail=1
    fi
  done
}

# scan must NOT sit on the right side of a pipeline: bash would run it
# in a subshell and its fail=1 would be lost. Process substitution keeps
# it in the parent shell.

# 1) Every commit in history.
for rev in $(git rev-list HEAD); do
  scan "${rev:0:7}" \
    < <(git ls-tree -r --format='%(objectsize)%x09%(objectname)%x09%(path)' "$rev")
done

# 2) The staged index — what a commit would actually record. The
#    working tree alone is not enough: a staged blob survives replacing
#    or deleting the file on disk.
scan index < <(
  git ls-files -s | while IFS=$'\t' read -r meta path; do
    [ -n "$path" ] || continue
    obj=${meta#* }   # "<mode> <object> <stage>" -> "<object> <stage>"
    obj=${obj%% *}   #                          -> "<object>"
    printf '%s\t%s\t%s\n' "$(git cat-file -s "$obj")" "$obj" "$path"
  done
)

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
