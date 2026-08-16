#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# DCO check: every commit reachable from HEAD must carry a
# Signed-off-by trailer (git commit -s). See CONTRIBUTING.md.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0
for rev in $(git rev-list HEAD); do
  if ! git log -1 --format='%(trailers:key=Signed-off-by,valueonly)' "$rev" \
      | grep -q '@'; then
    printf 'FAIL: no Signed-off-by on commit %s\n' \
      "$(git log -1 --format='%h %s' "$rev")"
    fail=1
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "check-dco: OK ($(git rev-list --count HEAD) commits)"
fi
exit "$fail"
