#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Host-time guard (#78): nothing under core/ may read the host's clock.
#
# Virtual time is the only clock the machine has (core/include/amberfolio/
# machine/clock.h), and that is what makes a run recordable and replayable
# (PLAN.md §4, docs/replay.md). docs/machine.md §4 stated the rule and
# said it was enforced in review; this is the mechanism. It greps core/
# for the ways a C++ program asks the operating system what time it is
# and fails on any of them. A comment that mentions one is caught too —
# the rule is not worth a special case, and core's own comments say
# "host time" rather than naming a header.
#
#     bash scripts/check-host-time.sh
set -euo pipefail
cd "$(dirname "$0")/.."

# Each alternative is a way to read a wall or monotonic clock, or the
# header that carries them. The libc `time()` is matched with the pointer
# argument it always takes, so `machine::time()`, `box.time()` and
# `ticks time() const` stay legal; `std::time(` is matched outright.
PATTERN='<chrono>|<ctime>|<sys/time\.h>|std::time\(|[^_[:alnum:]:.>]time\((NULL|nullptr|0|&)|clock_gettime|steady_clock|system_clock|high_resolution_clock|QueryPerformanceCounter|GetTickCount|gettimeofday|timeGetTime|std::chrono|emscripten_get_now|performance\.now'

fail=0
while IFS= read -r f; do
  # grep -n -E, and -H so the file is named; `|| true` because no match is
  # the success case.
  hits=$(grep -n -H -E "$PATTERN" "$f" || true)
  if [ -n "$hits" ]; then
    printf 'FAIL: host time under core/:\n%s\n' "$hits"
    fail=1
  fi
done < <(git ls-files 'core/*.cpp' 'core/*.h' 'core/*.h.in')

if [ "$fail" -eq 0 ]; then
  scanned=$(git ls-files 'core/*.cpp' 'core/*.h' 'core/*.h.in' | wc -l | tr -d ' ')
  echo "check-host-time: OK ($scanned files)"
fi
exit "$fail"
