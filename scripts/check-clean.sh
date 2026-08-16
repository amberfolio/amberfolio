#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Content guard: this repository must never contain material from the
# original games — no game code, data, or assets, in any form. Runs in CI
# on every push, from the repository's first commit onward.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0

# 1) Game-artifact filenames must never be committed.
#    (These filenames are publicly documented; the list reveals nothing.)
bad=$(git ls-files | grep -iE '(^|/)(start[^/]*\.exe|game\.ovr|pool\.cfg)$|\.(dax|sav|itm|spc)$' || true)
if [ -n "$bad" ]; then
  printf 'FAIL: game-artifact filename committed:\n%s\n' "$bad"
  fail=1
fi

# 2) No large blobs (256 KiB cap until a documented exception exists).
#    Original binaries and data files have no business here at any size;
#    the cap catches them and anything else that should be questioned.
while IFS= read -r f; do
  [ -f "$f" ] || continue
  size=$(wc -c < "$f")
  if [ "$size" -gt 262144 ]; then
    printf 'FAIL: oversized file %s (%s bytes)\n' "$f" "$size"
    fail=1
  fi
done < <(git ls-files)

if [ "$fail" -eq 0 ]; then
  echo "check-clean: OK"
fi
exit "$fail"
