#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Content guard: this repository must never contain material from the
# original games — no game code, data, or assets, in any form. Scans
# EVERY commit reachable from HEAD plus the working tree; runs in CI on
# every push (with full history fetched). This is a tripwire against
# obvious artifacts (denylisted filenames, oversized blobs) — the deeper
# clean-content claim is made inspectable by the full public history,
# not proven by this script.
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

scan() { # $1 = label; stdin = "size<TAB>path" lines
  local label="$1" size path
  while IFS=$'\t' read -r size path; do
    [ -n "$path" ] || continue
    if grep -qiE "$DENY" <<<"$path"; then
      printf 'FAIL[%s]: game-artifact filename: %s\n' "$label" "$path"
      fail=1
    fi
    if [ "$size" != "-" ] && [ "$size" -gt "$MAX" ]; then
      printf 'FAIL[%s]: oversized file %s (%s bytes)\n' "$label" "$path" "$size"
      fail=1
    fi
  done
}

# scan must NOT sit on the right side of a pipeline: bash would run it
# in a subshell and its fail=1 would be lost. Process substitution keeps
# it in the parent shell.

# 1) Every commit in history.
for rev in $(git rev-list HEAD); do
  scan "${rev:0:7}" < <(git ls-tree -r --format='%(objectsize)%x09%(path)' "$rev")
done

# 2) The working tree (tracked files), for local pre-commit use.
scan worktree < <(
  while IFS= read -r f; do
    [ -f "$f" ] || continue
    printf '%s\t%s\n' "$(wc -c < "$f")" "$f"
  done < <(git ls-files)
)

if [ "$fail" -eq 0 ]; then
  echo "check-clean: OK ($(git rev-list --count HEAD) commits + worktree)"
fi
exit "$fail"
