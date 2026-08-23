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

Two kinds of session (M4-R2, #101)
----------------------------------

Some sessions are of programs this repository owns, and their disk is
committed beside the recording.  Those run anywhere, and CI runs them.

A session recorded from a *game* cannot have its disk committed — the
disk is the player's own copy, and no byte of it may enter this tree
(PLAN.md §6).  The decision on #101 is that such a session is committed
anyway: the recording reproduces nothing, and what it needs from a disk
is names, sizes and digests, which are facts.  So its descriptor pins the
disk it wants, this script is told where a copy of that disk is, and when
it is not told — or when what it is told is a different disk — the
session is **skipped and said so**.

That makes three outcomes, and the third is the one that has to be
impossible to misread:

    ok      the target reproduced the recording
    FAIL    it did not, and that is a finding about the machine
    SKIP    nothing was checked, and here is what was missing

A sweep that verified nothing must never read as a sweep that passed, so
a skip is spelled in capitals beside `ok`, the summary names every
session that was skipped, and a run in which nothing verified at all says
that in as many words.

    python3 scripts/sweep.py --game-disk /path/to/a/pristine/copy
    AMBERFOLIO_GAME_DISK=/path/to/a/copy python3 scripts/sweep.py

`--game-disk` is repeatable, and a library of any size needs it to be: a
session begins wherever the last one left off, so the leg that loads a
saved game starts from the directory the leg that saved it wrote.  Which
candidate belongs to which session is never a guess — a descriptor pins
its disk exactly, so at most one of them can match.

Pinning a disk
--------------

    python3 scripts/sweep.py --pin city-shop --game-disk /path/to/copy

rewrites that session's descriptor with the tree it finds — every file's
DOS path, its size and its SHA-256, and every directory.  Run it once,
when the session is recorded, against the same directory the recording
was made over.  What lands in the tree is a list of names and digests and
nothing else.

PLAN.md §6 uses "sweep" for the playthrough sweep across all major
systems with the maintainer's own game copy.  That is #109's, and it will
be this script pointed at a fuller library than this one; the sessions
here are the same mechanism at the size a repository can hold.
"""

from __future__ import annotations

import argparse
import hashlib
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

# The environment variable that says where a copy of the game disk is, for
# the sessions whose disk cannot be committed.  A variable as well as a
# flag because a person sweeping repeatedly should not have to remember
# the path, and because CI will never have one — the absence is the
# normal case and has to be cheap.
GAME_DISK_ENV = "AMBERFOLIO_GAME_DISK"

# What `disk` says when the disk is the player's own and not in the tree.
EXTERNAL = "external"


class Descriptor:
    """What a `.session` file says: which disk this recording wants, and
    — when that disk is not in the tree — exactly which files it is.

    Deliberately not part of the recording's own grammar (machine/
    replay.h).  The recording's preamble pins what the *machine* saw: the
    root directory, in the VFS's pinned order.  This pins what is on the
    maintainer's shelf, all the way down, which is a different claim made
    by a different reader — and which matters because the game's saves
    live in a subdirectory the preamble does not descend into.  A disk
    whose `SAVE` is one run further along than it was would pass the
    preamble's check and then diverge halfway through, and a divergence
    is supposed to mean the machine changed.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.name = path.stem
        self.about = ""
        self.disk = ""
        # DOS path (upper case, backslash-separated) -> (size, digest), and
        # the set of directories. A directory has no digest; a digest of a
        # directory is not a thing (the recording's preamble says the same).
        self.files: dict[str, tuple[int, str]] = {}
        self.dirs: set[str] = set()
        self.problems: list[str] = []

        for number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            word = line.split(" ")
            if word[0] == "about":
                # Joined rather than replaced, so a description can wrap
                # to the width everything else in this tree wraps to.
                self.about = " ".join(
                    part for part in (self.about, " ".join(word[1:])) if part)
            elif word[0] == "disk" and len(word) == 2:
                self.disk = word[1]
            elif word[0] == "dir" and len(word) == 2:
                self.dirs.add(word[1].upper())
            elif word[0] == "file" and len(word) == 4:
                self.files[word[1].upper()] = (int(word[2]), word[3])
            else:
                self.problems.append(
                    f"{rel(path)}:{number}: not a descriptor line: {line!r}")

        if not self.disk:
            self.problems.append(f"{rel(path)}: no disk line")
        if self.disk != EXTERNAL and (self.files or self.dirs):
            # A committed disk is pinned by being committed. Two pins that
            # could disagree is one pin too many.
            self.problems.append(
                f"{rel(path)}: a disk in the tree is pinned by git;"
                " file and dir lines belong to an external one")

    @property
    def external(self) -> bool:
        return self.disk == EXTERNAL


class Session:
    """A recording, its descriptor, and what it says it is about."""

    def __init__(self, path: Path, descriptor: Descriptor | None) -> None:
        self.path = path
        self.name = path.stem
        self.descriptor = descriptor
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
    def external(self) -> bool:
        return self.descriptor is not None and self.descriptor.external

    def disk(self, game_disks: list[Path]) -> Path | None:
        """Where the disk this recording wants actually is, or None when
        it is an external one and none of the given directories is it.

        More than one may be given, and a session library of any size
        needs that: a session begins wherever the last one left off, and
        `docs/playable.md`'s load leg starts from the directory its save
        leg wrote. Which is which is never a guess — a descriptor pins its
        disk exactly, so at most one of the candidates can match.
        """
        if self.descriptor is None:
            return None
        if not self.descriptor.external:
            return SESSIONS / self.descriptor.disk
        for candidate in game_disks:
            if candidate.is_dir() and check_pinned(self.descriptor,
                                                   candidate)[0]:
                return candidate
        return None

    def problems(self) -> list[str]:
        """What is wrong with this session as a *session*, before any
        machine runs — a recording naming a file nobody committed cannot
        be verified by anything, and saying so beats four identical
        failures."""
        wrong = []
        if self.descriptor is None:
            wrong.append(
                f"no descriptor at {rel(self.path.with_suffix('.session'))};"
                " a recording has to say which disk it wants")
            return wrong
        wrong += self.descriptor.problems
        if self.program is None:
            wrong.append("no program line")
        if wrong or self.descriptor.external:
            return wrong
        disk = SESSIONS / self.descriptor.disk
        if not disk.is_dir():
            wrong.append(f"no disk directory at {rel(disk)}")
            return wrong
        for name in self.files:
            if not (disk / name).is_file():
                wrong.append(f"the manifest names {name}, which the disk lacks")
        return wrong


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace(os.sep, "/")
    except ValueError:
        return str(path)


def digest_of(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            sha.update(block)
    return sha.hexdigest()


def walk_disk(root: Path) -> tuple[dict[str, tuple[int, str]], set[str]]:
    """Every file and directory under `root`, by DOS-shaped path.

    Upper case and backslash-separated, because that is what the machine
    calls them and what a descriptor is read beside; and case-folded
    because two hosts disagree about whether a name has a case at all.
    """
    files: dict[str, tuple[int, str]] = {}
    dirs: set[str] = set()
    for here, subdirs, names in os.walk(root):
        base = Path(here).relative_to(root)
        for name in subdirs:
            dirs.add(str(base / name).replace(os.sep, "\\").upper())
        for name in names:
            full = Path(here) / name
            key = str(base / name).replace(os.sep, "\\").upper()
            files[key] = (full.stat().st_size, digest_of(full))
    return files, dirs


def check_pinned(descriptor: Descriptor,
                 disk: Path) -> tuple[bool, str]:
    """Is `disk` the disk this descriptor pins?

    Exactly, in both directions.  A file the pin does not name is as much
    "not that disk" as a file it names and cannot find: the program opens
    its saves by name rather than by listing them, so an extra one is
    invisible *today* and the day it stops being invisible is the day
    this check would have been the only warning.  A session is recorded
    against a pristine snapshot (`docs/playable.md`), and comparing
    against one is the whole reason the snapshot exists.

    The answer is never "fail".  A disk that is not the recorded one says
    nothing about the emulator, and reporting it as a divergence would be
    a finding about the machine that is really a finding about a
    directory.
    """
    found, found_dirs = walk_disk(disk)

    missing = sorted(set(descriptor.files) - set(found))
    if missing:
        return False, (f"{rel(disk)} lacks {missing[0]}"
                       + (f" and {len(missing) - 1} more"
                          if len(missing) > 1 else ""))

    extra = sorted(set(found) - set(descriptor.files))
    if extra:
        return False, (f"{rel(disk)} also holds {extra[0]}"
                       + (f" and {len(extra) - 1} more"
                          if len(extra) > 1 else ""))

    for name, (size, digest) in sorted(descriptor.files.items()):
        if found[name][0] != size:
            return False, (f"{name} is {found[name][0]} bytes,"
                           f" and the session was recorded over {size}")
        if found[name][1] != digest:
            return False, f"{name} is not the file the session names"

    missing_dirs = sorted(descriptor.dirs - found_dirs)
    if missing_dirs:
        return False, f"{rel(disk)} has no {missing_dirs[0]} directory"

    return True, f"{rel(disk)}, {len(descriptor.files)} file(s) as recorded"


def find_sessions(only: str | None) -> list[Session]:
    found = sorted(SESSIONS.glob("*.rec"))
    if only is not None:
        found = [p for p in found if p.stem == only]
        if not found:
            sys.exit(f"sweep: no session called {only!r} in {rel(SESSIONS)}")
    sessions = []
    for path in found:
        side = path.with_suffix(".session")
        sessions.append(Session(path, Descriptor(side) if side.is_file()
                                else None))
    return sessions


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


def sweep_desktop(host: Path, session: Session, disk: Path) -> tuple[str, str]:
    """The desktop host replaying the session over a copy of its disk.

    A copy, because a program may write to it: a replay is a run of the
    same *initial* conditions, and the preamble's manifest is checked
    against the disk before a step is taken.  Replaying over a disk a
    previous run left behind would fail that check correctly and prove
    nothing about the machine.

    For a game session the copy is doing a second job as well.  Driving
    the game by hand means restoring a snapshot between runs and
    remembering to (`docs/playable.md` says so twice, having learned it);
    a sweep cannot forget, which is most of what makes a recorded leg
    repeatable where a scripted one is not.
    """
    with tempfile.TemporaryDirectory(prefix="amberfolio-sweep-") as work:
        copy = Path(work) / "disk"
        shutil.copytree(disk, copy)
        done = run(
            [
                str(host), str(copy), str(session.program),
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
        return [("(suites)", label, "SKIP", "ctest is not on PATH")
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
            rows.append(("(suites)", label, "SKIP",
                         f"no configured tree builds {pattern}"))
    return rows


def pin(name: str, game_disk: Path | None) -> int:
    """Rewrite a session's descriptor with the tree `game_disk` holds.

    Names, sizes and digests: facts about a disk, and the only thing
    about a player's copy that may be written down (CONTRIBUTING.md).
    """
    side = SESSIONS / f"{name}.session"
    if not side.is_file():
        sys.exit(f"sweep: no descriptor at {rel(side)}")
    descriptor = Descriptor(side)
    if not descriptor.external:
        sys.exit(f"sweep: {rel(side)} names a disk in the tree, which git"
                 " pins already")
    if game_disk is None:
        sys.exit(f"sweep: --pin needs --game-disk (or ${GAME_DISK_ENV})")
    if not game_disk.is_dir():
        sys.exit(f"sweep: {game_disk} is not a directory")

    files, dirs = walk_disk(game_disk)
    kept = [line for line in side.read_text(encoding="utf-8").splitlines()
            if not line.startswith(("file ", "dir "))]
    while kept and not kept[-1].strip():
        kept.pop()
    lines = kept + [""]
    lines += [f"dir {name}" for name in sorted(dirs)]
    lines += [f"file {name} {size} {digest}"
              for name, (size, digest) in sorted(files.items())]
    # The newline spelled out: on Windows the default would write CRLF,
    # and a descriptor whose line endings changed every time it was
    # pinned would come back as a diff of the whole file.
    with side.open("w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print(f"sweep: pinned {len(files)} file(s) and {len(dirs)} directory(s)"
          f" from {game_disk} into {rel(side)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify every committed session on every available target")
    parser.add_argument("--build", action="append", default=[],
                        help="a build tree to run out of; repeatable"
                             " (default: every configured tree under build/)")
    parser.add_argument("--session", help="only this one, by stem")
    parser.add_argument("--targets", default="sdl,ctest",
                        help="comma-separated: sdl, ctest (default: both)")
    parser.add_argument("--game-disk", action="append", default=[],
                        help="a copy of a disk the game sessions were"
                             f" recorded against; repeatable (or"
                             f" ${GAME_DISK_ENV}, {os.pathsep}-separated)")
    parser.add_argument("--pin", metavar="SESSION",
                        help="rewrite that session's descriptor with the"
                             " names, sizes and digests --game-disk holds")
    args = parser.parse_args()
    wanted = {t.strip() for t in args.targets.split(",") if t.strip()}
    game_disks = [Path(p) for p in args.game_disk]
    if not game_disks:
        game_disks = [Path(p) for p
                      in os.environ.get(GAME_DISK_ENV, "").split(os.pathsep)
                      if p]

    if args.pin is not None:
        if len(game_disks) != 1:
            sys.exit("sweep: --pin takes exactly one --game-disk, the"
                     " snapshot the session was recorded over")
        return pin(args.pin, game_disks[0])

    sessions = find_sessions(args.session)
    if not sessions:
        print(f"sweep: no sessions in {rel(SESSIONS)}", file=sys.stderr)
        return 1

    trees = find_build_trees(args.build)
    rows: list[tuple[str, str, str, str]] = []
    failures = 0
    verified = 0
    skipped: list[str] = []

    for session in sessions:
        wrong = session.problems()
        if wrong:
            for problem in wrong:
                rows.append((session.name, "session", "FAIL", problem))
                failures += 1
            continue

        # A session whose disk is not in the tree is not something the
        # native suite or the wasm module can be handed: both read the
        # session directory as source. Said per session rather than left
        # out, because a table that simply omitted them would read as a
        # table of everything.
        if session.external and "ctest" in wanted:
            for label, _ in SUITES:
                rows.append((session.name, label, "SKIP",
                             "the suites verify sessions whose disk is"
                             " committed; this one's cannot be"))

        if "sdl" not in wanted:
            continue

        disk = session.disk(game_disks)
        if disk is None:
            # Why, and not just that: "no disk" and "a disk that has moved
            # on since" are different things to be told, and the second is
            # the one somebody can act on.
            if not game_disks:
                why = f"no --game-disk given (or ${GAME_DISK_ENV})"
            else:
                why = "; ".join(
                    check_pinned(session.descriptor, candidate)[1]
                    if candidate.is_dir() else f"{candidate} is not a directory"
                    for candidate in game_disks)
            rows.append((session.name, "sdl", "SKIP",
                         f"this disk is not that disk: {why}"))
            skipped.append(session.name)
            continue

        host = find_desktop_host(trees)
        if host is None:
            rows.append((session.name, "sdl", "SKIP",
                         "no desktop host in the build tree"))
            skipped.append(session.name)
            continue
        state, detail = sweep_desktop(host, session, disk)
        rows.append((session.name, "sdl", state, detail))
        failures += state == "FAIL"
        verified += state == "ok"

    if "ctest" in wanted:
        if not trees:
            for label, _ in SUITES:
                rows.append(("(suites)", label, "SKIP",
                             "no configured build tree"))
        else:
            for row in sweep_suites(trees):
                rows.append(row)
                failures += row[2] == "FAIL"
                verified += row[2] == "ok"

    width = max((len(r[0]) for r in rows), default=8)
    print()
    for name, target, state, detail in rows:
        print(f"  {name:<{width}}  {target:<8} {state:<5} {detail}")
    print()

    if trees:
        print("sweep: build trees " + ", ".join(rel(t) for t in trees))
    passes = sum(1 for r in rows if r[2] == "ok")
    skips = sum(1 for r in rows if r[2] == "SKIP")
    print(f"sweep: {len(sessions)} session(s), {len(rows)} check(s),"
          f" {passes} verified, {failures} failure(s), {skips} skipped")

    if skipped:
        # Named, because "skipped" as a number beside a green summary is
        # how a sweep that checked nothing gets read as a sweep that
        # passed. The decision on #101 asks for exactly this.
        print("sweep: NOT VERIFIED, disk absent or different: "
              + ", ".join(sorted(set(skipped))))
    if verified == 0:
        print("sweep: NOTHING WAS VERIFIED — every check was skipped."
              " This is not a pass.", file=sys.stderr)
        return 1

    if failures:
        print("sweep: tests/sessions/README.md says when a session may"
              " legitimately be re-recorded. If none of those changed, a"
              " divergence is a finding about the machine.", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
