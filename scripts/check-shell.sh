#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Lint gate for the shell scripts. They are load-bearing — check-clean.sh
# is the content guard the whole clean-content claim leans on — and shell
# is a language where a quoting mistake fails open rather than loudly. A
# subshell bug once swallowed a failure flag in check-clean.sh; shellcheck
# is the second pair of eyes on that class of mistake, and test-guards.sh
# is the first.
#
# Set SHELLCHECK to point at a specific binary.
set -euo pipefail
cd "$(dirname "$0")/.."

: "${SHELLCHECK:=shellcheck}"

if ! command -v "$SHELLCHECK" >/dev/null 2>&1; then
  echo "check-shell: '$SHELLCHECK' not found." >&2
  echo "  apt-get install shellcheck | brew install shellcheck" >&2
  exit 127
fi

mapfile -t files < <(git ls-files '*.sh')

if [ "${#files[@]}" -eq 0 ]; then
  echo "check-shell: no shell scripts tracked" >&2
  exit 1
fi

# --severity=style: everything shellcheck has to say, not just the errors.
# The scripts are few and short enough to hold to that; loosen it when
# that stops being true, not before.
"$SHELLCHECK" --severity=style --color=never "${files[@]}"
echo "check-shell: OK (${#files[@]} scripts)"
