#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# DCO check: every non-merge commit reachable from HEAD must carry a
# well-formed "Signed-off-by: Name <email>" trailer (git commit -s).
# Merge commits are exempt (GitHub's merge button does not sign them).
# See CONTRIBUTING.md.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0
for rev in $(git rev-list --no-merges HEAD); do
  if ! git log -1 --format='%(trailers:key=Signed-off-by,valueonly)' "$rev" \
      | grep -qE '^[^<>]+ <[^<>@[:space:]]+@[^<>@[:space:]]+>$'; then
    printf 'FAIL: no well-formed Signed-off-by on commit %s\n' \
      "$(git log -1 --format='%h %s' "$rev")"
    fail=1
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "check-dco: OK ($(git rev-list --no-merges --count HEAD) commits)"
fi
exit "$fail"
