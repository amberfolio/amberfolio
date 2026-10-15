#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Host-test hermeticity guard (#445): every test that starts the desktop
# host says which config it runs under.
#
# `amberfolio-sdl` reads the config file of whoever runs it
# (`%APPDATA%\amberfolio\config.txt` and its platform equivalents,
# docs/hosts.md §2a): seams, speed, `save-sidecars`. A test that does not
# say `--no-config` (ignore it) or `--config PATH` (a file the test owns)
# gives a different answer on a maintainer's desk than on a CI runner,
# and that is a check whose answer depends on whose desk it runs at. This
# found `sdl-host-ingests-a-journal` failing for the one person whose
# config said `save-sidecars on`.
#
# What is read, under hosts/sdl/:
#   - every `execute_process(...)` in cmake/*.cmake that runs `${HOST}`
#     must carry one of the two flags. A call that only forwards its
#     arguments (`${ARGN}`, `${RUN_ARGS}`) passes if the function it sits
#     in carries it, or else if every call of that function does.
#   - every `add_test(...)` in CMakeLists.txt that runs `amberfolio-sdl`
#     directly (the others hand the binary to a cmake/ script as -DHOST).
#
#     bash scripts/check-host-tests.sh
set -euo pipefail
cd "$(dirname "$0")/.."

# Run, not merely looked up: see check-tidy.sh for the Windows stub that
# answers `command -v` and executes nothing.
py=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1 &&
     "$candidate" -c "" >/dev/null 2>&1; then
    py="$candidate"
    break
  fi
done
if [ -z "$py" ]; then
  echo "check-host-tests: no working python, and reading CMake calls needs one" >&2
  exit 127
fi

"$py" - <<'PY'
import re
import subprocess
import sys

FLAG = re.compile(r"(?<![\w-])--(?:no-)?config(?![\w-])")


def code(text):
    # Blanked rather than dropped, so a reported line number is the file's.
    return "\n".join("" if l.lstrip().startswith("#") else l for l in text.split("\n"))


def call_end(text, open_at):
    """Index of the paren closing the one at `open_at`, quotes respected."""
    depth, quoted, i = 0, False, open_at
    while i < len(text):
        c = text[i]
        if c == "\\":
            i += 2
            continue
        if c == '"':
            quoted = not quoted
        elif not quoted:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    return len(text)


def calls(text, name):
    """(start, call text) for each `name(...)`, not the `function(name` line."""
    out = []
    for m in re.finditer(r"(?<![\w-])" + re.escape(name) + r"\s*\(", text):
        before = text[: m.start()].rstrip()
        if before.endswith("function("):
            continue
        end = call_end(text, m.end() - 1)
        out.append((m.start(), text[m.start() : end + 1]))
    return out


listed = subprocess.run(
    ["git", "ls-files", "hosts/sdl/cmake/*.cmake", "hosts/sdl/CMakeLists.txt"],
    check=True,
    capture_output=True,
    text=True,
).stdout.split("\n")
paths = [p for p in listed if p]

bad, seen = [], 0
for path in paths:
    with open(path, encoding="utf-8") as f:
        text = code(f.read())

    def line_of(at):
        return text.count("\n", 0, at) + 1

    if path.endswith("CMakeLists.txt"):
        for at, blk in calls(text, "add_test"):
            if re.search(r"COMMAND\s+amberfolio-sdl(?![\w-])", blk):
                seen += 1
                if not FLAG.search(blk):
                    bad.append(
                        f"{path}:{line_of(at)}: add_test runs the host with"
                        " neither --no-config nor --config"
                    )
        continue
    for at, blk in calls(text, "execute_process"):
        if "${HOST}" not in blk:
            continue
        seen += 1
        if FLAG.search(blk):
            continue
        fn = None
        if re.search(r"\$\{\w*(?:ARGN|ARGS)\}", blk):
            for fm in re.finditer(r"function\s*\(\s*(\w+)", text[:at]):
                fn = fm
        if fn is None:
            bad.append(
                f"{path}:{line_of(at)}: execute_process runs ${{HOST}} with"
                " neither --no-config nor --config"
            )
            continue
        for cat, cblk in calls(text, fn.group(1)):
            if not FLAG.search(cblk):
                bad.append(
                    f"{path}:{line_of(cat)}: {fn.group(1)}() reaches the host"
                    " with neither --no-config nor --config"
                )

if seen == 0:
    print("FAIL: check-host-tests found no host invocation to check;"
          " the guard no longer matches anything")
    sys.exit(1)
if bad:
    print("FAIL: a host test that reads the developer's config (#445):")
    for b in bad:
        print("  " + b)
    sys.exit(1)
print(f"check-host-tests: OK ({seen} host invocations)")
PY
