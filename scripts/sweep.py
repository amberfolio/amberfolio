#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Run every committed session against every target that can verify one.

A session is a recording plus the disk it was recorded against
(``tests/sessions/README.md``).  Verifying one is the claim M4-R1 exists
to make: a run is *keys, ticks and hashes*, so a target that reproduces a
recording has reproduced every byte of RAM, every device's registers, the
scheduler's deadlines and the framebuffer, at every checkpoint, from the
same starting conditions.

The point of running them all together is that the targets share almost
nothing.  The desktop host is MSVC or GCC or Clang over SDL; the wasm
module is Emscripten's toolchain and its own build of SHA-256; the native
suite is a third compilation of the same core.  A build that agreed about
a program's *answer* and disagreed about the machine underneath it fails
here and nowhere else.

    python3 scripts/sweep.py                     # every session, every target
    python3 scripts/sweep.py --build build/linux-gcc --build build/wasm
    python3 scripts/sweep.py --session spin      # just one
    python3 scripts/sweep.py --targets sdl       # skip the test suites

Exit status is zero when every session verified on every target that was
found.  A target that is not built is *skipped and said so*, never
silently counted as a pass — a sweep that reported "all green" because it
found nothing to run would be the one failure this whole apparatus exists
to make impossible.

PLAN.md §6 uses "sweep" for the playthrough sweep across all major
systems with the maintainer's own game copy.  That is #101, and it will
be this script pointed at sessions recorded from a real playthrough; the
committed ones here are the same mechanism at a size CI can hold.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SESSIONS = ROOT / "tests" / "sessions"

# SDL's headless drivers, the same ones the smoke checks use — see
# hosts/sdl/cmake/run-verify-program.cmake for why `dummy` and not
# `offscreen`.  A sweep must not need a display.
HEADLESS_ENV = {
    "SDL_VIDEODRIVER": "dummy",
    "SDL_AUDIODRIVER": "dummy",
    "SDL_RENDER_DRIVER": "software",
}


class Session:
    """A recording, and what it says it is about."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.name = path.stem
        self.program: str | None = None
        self.files: list[str] = []
        self.seams: list[str] = []
        for line in path.read_text(encoding="latin-1").splitlines():
            word = line.split(" ")
            if word[0] == "program" and len(word) >= 2:
                self.program = word[1]
            elif word[0] == "file" and len(word) >= 2:
                self.files.append(word[1])
            elif word[0] == "seam" and len(word) >= 2:
                self.seams.append(word[1])

    @property
    def disk(self) -> Path:
        """The directory holding the files the manifest names."""
        return self.path.with_suffix("")

    def problems(self) -> list[str]:
        """What is wrong with this session as a *session*, before any
        machine runs — a recording naming a file nobody committed cannot
        be verified by anything, and saying so beats four identical
        failures."""
        wrong = []
        if self.program is None:
            wrong.append("no program line")
        if not self.disk.is_dir():
            wrong.append(f"no disk directory at {rel(self.disk)}")
            return wrong
        for name in self.files:
            if not (self.disk / name).is_file():
                wrong.append(f"the manifest names {name}, which the disk lacks")
        return wrong


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace(os.sep, "/")
    except ValueError:
        return str(path)


def find_sessions(only: str | None) -> list[Session]:
    found = sorted(SESSIONS.glob("*.rec"))
    if only is not None:
        found = [p for p in found if p.stem == only]
        if not found:
            sys.exit(f"sweep: no session called {only!r} in {rel(SESSIONS)}")
    return [Session(p) for p in found]


def find_build_trees(explicit: list[str]) -> list[Path]:
    """Every configured build tree to run out of.

    Every one, and not the newest: the targets live in different trees.
    The desktop host is in a native tree and the wasm module is in the
    Emscripten one, and a sweep that picked a single tree would silently
    check half of what it claims to.
    """
    if explicit:
        trees = []
        for name in explicit:
            tree = Path(name)
            if not (tree / "CMakeCache.txt").is_file():
                sys.exit(f"sweep: {name} is not a configured build tree")
            trees.append(tree)
        return trees
    return sorted(p.parent for p in (ROOT / "build").glob("*/CMakeCache.txt"))


def config_of(tree: Path) -> str | None:
    """The configuration to ask CTest for, on a multi-config generator.

    Ninja Multi-Config and Visual Studio put every configuration in one
    tree, and CTest run without `-C` there finds no tests and says so as
    an error — which a sweep would otherwise report as a divergence.
    """
    cache = (tree / "CMakeCache.txt").read_text(encoding="utf-8",
                                                errors="replace")
    for line in cache.splitlines():
        if line.startswith("CMAKE_CONFIGURATION_TYPES:"):
            _, _, value = line.partition("=")
            configs = [c for c in value.split(";") if c]
            if configs:
                return "Debug" if "Debug" in configs else configs[0]
    return None


def find_desktop_host(trees: list[Path]) -> Path | None:
    for tree in trees:
        for name in ("amberfolio", "amberfolio.exe"):
            for candidate in tree.glob(f"hosts/sdl/**/{name}"):
                if candidate.is_file():
                    return candidate
    return None


def run(command: list[str], env: dict[str, str] | None = None):
    merged = dict(os.environ)
    if env:
        merged.update(env)
    return subprocess.run(
        command, capture_output=True, text=True, env=merged, cwd=ROOT,
        check=False, errors="replace",
    )


def sweep_desktop(host: Path, session: Session) -> tuple[str, str]:
    """The desktop host replaying the session over a copy of its disk.

    A copy, because a program may write to it: a replay is a run of the
    same *initial* conditions, and the preamble's manifest is checked
    against the disk before a step is taken.  Replaying over a disk a
    previous run left behind would fail that check correctly and prove
    nothing about the machine.
    """
    with tempfile.TemporaryDirectory(prefix="amberfolio-sweep-") as work:
        disk = Path(work) / "disk"
        shutil.copytree(session.disk, disk)
        done = run(
            [
                str(host), str(disk), str(session.program),
                "--headless", "--replay", str(session.path),
            ],
            HEADLESS_ENV,
        )
    for line in done.stderr.splitlines():
        if "amberfolio: replay " in line:
            return ("ok" if done.returncode == 0 else "FAIL"), line.strip()
    return "FAIL", f"the host said nothing about the replay (exit {done.returncode})"


# The suites that verify a session from inside their own test binary, and
# the CTest name pattern that selects them.  Run through CTest rather than
# invoked directly, so that what a sweep runs is what CI runs.
SUITES = (
    ("native", "SessionLibrary"),
    ("wasm", "wasm-module-smoke"),
)


def sweep_suites(trees: list[Path]) -> list[tuple[str, str, str, str]]:
    """Every suite, in every tree that has it."""
    ctest = shutil.which("ctest")
    if ctest is None:
        return [("(suites)", label, "skip", "ctest is not on PATH")
                for label, _ in SUITES]

    rows: list[tuple[str, str, str, str]] = []
    for label, pattern in SUITES:
        ran_somewhere = False
        for tree in trees:
            command = [ctest, "--test-dir", str(tree)]
            config = config_of(tree)
            if config is not None:
                command += ["-C", config]
            command += ["-R", pattern, "--output-on-failure"]
            done = run(command)
            output = done.stdout + done.stderr
            if "No tests were found" in output:
                continue  # This tree does not build that target; try the next.
            ran_somewhere = True
            if done.returncode == 0:
                passed = [ln for ln in output.splitlines()
                          if "tests passed" in ln]
                rows.append(("(suites)", label, "ok",
                             f"{tree.name}: "
                             + (passed[-1].strip() if passed else "passed")))
            else:
                said = [ln.strip() for ln in output.splitlines()
                        if "replay diverged" in ln or "replay refused" in ln
                        or "did not verify" in ln]
                rows.append(("(suites)", label, "FAIL",
                             f"{tree.name}: "
                             + (said[0] if said else f"ctest -R {pattern} failed")))
        if not ran_somewhere:
            # Said out loud, never counted as a pass: a sweep that reported
            # green because it found nothing to run is the one failure this
            # apparatus exists to make impossible.
            rows.append(("(suites)", label, "skip",
                         f"no configured tree builds {pattern}"))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify every committed session on every available target")
    parser.add_argument("--build", action="append", default=[],
                        help="a build tree to run out of; repeatable"
                             " (default: every configured tree under build/)")
    parser.add_argument("--session", help="only this one, by stem")
    parser.add_argument("--targets", default="sdl,ctest",
                        help="comma-separated: sdl, ctest (default: both)")
    args = parser.parse_args()
    wanted = {t.strip() for t in args.targets.split(",") if t.strip()}

    sessions = find_sessions(args.session)
    if not sessions:
        print(f"sweep: no sessions in {rel(SESSIONS)}", file=sys.stderr)
        return 1

    trees = find_build_trees(args.build)
    rows: list[tuple[str, str, str, str]] = []
    failures = 0
    skips = 0

    for session in sessions:
        wrong = session.problems()
        if wrong:
            for problem in wrong:
                rows.append((session.name, "session", "FAIL", problem))
                failures += 1
            continue

        if "sdl" in wanted:
            host = find_desktop_host(trees)
            if host is None:
                rows.append((session.name, "sdl", "skip",
                             "no desktop host in the build tree"))
                skips += 1
            else:
                state, detail = sweep_desktop(host, session)
                rows.append((session.name, "sdl", state, detail))
                failures += state == "FAIL"

    if "ctest" in wanted:
        if not trees:
            for label, _ in SUITES:
                rows.append(("(suites)", label, "skip",
                             "no configured build tree"))
                skips += 1
        else:
            for row in sweep_suites(trees):
                rows.append(row)
                failures += row[2] == "FAIL"
                skips += row[2] == "skip"

    width = max((len(r[0]) for r in rows), default=8)
    print()
    for name, target, state, detail in rows:
        print(f"  {name:<{width}}  {target:<8} {state:<5} {detail}")
    print()

    if trees:
        print("sweep: build trees " + ", ".join(rel(t) for t in trees))
    print(f"sweep: {len(sessions)} session(s), {len(rows)} check(s),"
          f" {failures} failure(s), {skips} skipped")

    if failures:
        print("sweep: tests/sessions/README.md says when a session may"
              " legitimately be re-recorded. If none of those changed, a"
              " divergence is a finding about the machine.", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
