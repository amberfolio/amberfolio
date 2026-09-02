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
STORE_ENV = "AMBERFOLIO_JOURNAL_STORE"
DOCUMENT_ENV = "AMBERFOLIO_DOCUMENT"

# What `disk` says when the disk is the player's own and not in the tree.
EXTERNAL = "external"


class Descriptor:
    """What a `.session` file says: which disk this recording wants, and
    — when that disk is not in the tree — exactly which files it is.

    Deliberately not part of the recording's own grammar (machine/
    replay.h).  The recording's preamble pins what the *machine* saw;
    this pins what is on the maintainer's shelf, which is a different
    claim made by a different reader, and it is what *matches* a
    candidate directory to a session — `--game-disk` is repeatable, and
    at most one of the candidates can be the disk a descriptor names.

    It also mattered for a second reason until #155.  The committed
    recordings are format 1, whose preamble walks the root only, and the
    game's saves live in `\\SAVE\\`: a disk whose save directory was one
    run further along than it was would pass such a preamble's check and
    then diverge halfway through, and a divergence is supposed to mean
    the machine changed.  Format 2 recurses, in this same `\\`-joined
    spelling, so a recording made from now on closes that itself — but
    the six game sessions here cannot be re-recorded from this tree, and
    matching a disk to a session is still this file's job either way.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.name = path.stem
        self.about = ""
        self.disk = ""
        # The session this one is the same run as, with one thing
        # changed. See `contrast_of()` below for what that buys.
        self.contrast = ""
        # The journal store this recording was made over, if any: a path
        # in the tree, or EXTERNAL with a digest. See `store()` below for
        # why a recording needs to name one at all.
        self.store = ""
        self.store_digest = ""
        # Documents this recording's seams are gated on, by digest. A
        # document is never committed here (PLAN.md §6), so there is one
        # form and it is a fingerprint.
        self.documents: list[str] = []
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
            elif word[0] == "contrast" and len(word) == 2:
                self.contrast = word[1]
            elif word[0] == "document" and len(word) == 2:
                self.documents.append(word[1])
            elif word[0] == "journal-store" and len(word) in (2, 3):
                self.store = word[1]
                self.store_digest = word[2] if len(word) == 3 else ""
            elif word[0] == "dir" and len(word) == 2:
                self.dirs.add(word[1].upper())
            elif word[0] == "file" and len(word) == 4:
                self.files[word[1].upper()] = (int(word[2]), word[3])
            else:
                self.problems.append(
                    f"{rel(path)}:{number}: not a descriptor line: {line!r}")

        if not self.disk:
            self.problems.append(f"{rel(path)}: no disk line")
        if self.store == EXTERNAL and not self.store_digest:
            # The whole point of pinning it: a store outside the tree is
            # a file nobody here can see, so the digest is the only thing
            # that says the one on this machine is the one the recording
            # was made over.
            self.problems.append(
                f"{rel(path)}: `journal-store external` wants a sha256"
                " after it")
        if self.store and self.store != EXTERNAL and self.store_digest:
            self.problems.append(
                f"{rel(path)}: a store in the tree is pinned by git;"
                " a digest belongs to an external one")
        if self.disk != EXTERNAL and (self.files or self.dirs):
            # A committed disk is pinned by being committed. Two pins that
            # could disagree is one pin too many.
            self.problems.append(
                f"{rel(path)}: a disk in the tree is pinned by git;"
                " file and dir lines belong to an external one")

    @property
    def external(self) -> bool:
        return self.disk == EXTERNAL

    @property
    def external_store(self) -> bool:
        return self.store == EXTERNAL


class Session:
    """A recording, its descriptor, and what it says it is about."""

    def __init__(self, path: Path, descriptor: Descriptor | None) -> None:
        self.path = path
        self.name = path.stem
        self.descriptor = descriptor
        self.program: str | None = None
        # Manifest paths, `\`-joined and relative to the root.  In format
        # 1 they are bare names and a directory arrives as a `file` line
        # with a zero size and a zero digest; in format 2 they recurse and
        # a directory has its own `dir` line (docs/replay.md §1).  Kept
        # apart so the check below can say which is which.
        self.format = 0
        self.files: list[str] = []
        self.dirs: list[str] = []
        self.seams: list[str] = []
        for line in path.read_text(encoding="latin-1").splitlines():
            word = line.split(" ")
            if word[0] == "amberfolio-recording" and len(word) >= 2:
                self.format = int(word[1]) if word[1].isdigit() else 0
            elif word[0] == "program" and len(word) >= 2:
                self.program = word[1]
            elif word[0] == "file" and len(word) >= 2:
                self.files.append(word[1])
            elif word[0] == "dir" and len(word) >= 2:
                self.dirs.append(word[1])
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

    def store(self, stores: list[Path]) -> tuple[Path | None, str]:
        """The journal store this recording wants, and what is wrong.

        A recording is keys, ticks and hashes (`docs/replay.md`), and the
        journal reader's other input is a *file* — the player's own store.
        What the reader draws out of it is in the framebuffer, and the
        framebuffer is in every checkpoint hash, so a replay handed a
        different store than the recording was made over diverges. That
        is #175's loose end, and a `journal-store` line is the whole of
        the fix: the descriptor names the file the way it already names
        the disk.

        A store committed here is found by its path. An external one — a
        player's own ingestion, which may never enter this tree — is
        pinned by digest and looked for among the `--journal-store`
        directories or files this run was given, and when none of them is
        it, the session is **skipped and said so**.
        """
        if self.descriptor is None or not self.descriptor.store:
            return None, ""
        if not self.descriptor.external_store:
            here = ROOT / self.descriptor.store
            if not here.is_file():
                return None, f"no store at {rel(here)}"
            return here, ""
        want = self.descriptor.store_digest
        for candidate in stores:
            if candidate.is_file() and digest_of(candidate) == want:
                return candidate, ""
        return None, (f"no journal store with digest {want[:12]}...; pass"
                      f" --journal-store, or set {STORE_ENV}")

    def documents(self, held: list[Path]) -> tuple[list[Path], str]:
        """The documents this recording's seams are gated on.

        A possession gate is a seam refusing to arm until the player
        presents the document the enhancement is *for* (#115, #171,
        PLAN.md §5). So a recording made with a gated seam on cannot be
        replayed by somebody who does not hold that document — and the
        machine says so rather than diverging: the replay is **refused**,
        naming the condition.

        A document is bytes of somebody's own PDF and never enters this
        tree, so the descriptor pins it the way it pins an external disk
        or an external store: by digest, and the sweep looks for a file
        with that digest among the ones it was given.
        """
        if self.descriptor is None or not self.descriptor.documents:
            return [], ""
        found: list[Path] = []
        for want in self.descriptor.documents:
            here = next((c for c in held
                         if c.is_file() and digest_of(c) == want), None)
            if here is None:
                return [], (f"no document with digest {want[:12]}...; pass"
                            f" --document, or set {DOCUMENT_ENV}")
            found.append(here)
        return found, ""

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
        # A manifest path is the machine's spelling; `\` is its separator
        # on every host, so it is split rather than handed to Path().  A
        # format-1 `file` line may be a directory (that is how format 1
        # wrote one), so only a `dir` line is asked to be one.
        for name in self.files:
            here = disk.joinpath(*name.split("\\"))
            ok = here.exists() if self.format < 2 else here.is_file()
            if not ok:
                wrong.append(f"the manifest names {name}, which the disk lacks")
        for name in self.dirs:
            if not disk.joinpath(*name.split("\\")).is_dir():
                wrong.append(
                    f"the manifest names {name}, which is not a directory"
                    " on the disk")
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


def checkpoints_of(path: Path) -> list[tuple[int, str]]:
    """Every checkpoint in a recording: its tick and its whole-state hash."""
    marks = []
    for line in path.read_text(encoding="latin-1").splitlines():
        word = line.split(" ")
        if word[0] == "checkpoint" and len(word) >= 4:
            marks.append((int(word[1]), word[3]))
    return marks


def contrast_of(session: Session,
                partner: Session) -> tuple[str, str]:
    """Does this pair actually differ, and only where it should?

    Two recordings of the *same script over the same disk*, one thing
    apart — a seam on rather than off — must agree exactly until that one
    thing first matters, and disagree from there on. That is the check
    `docs/seams.md` asks for after a seam has been wrong twice
    (#129, #130): a seam that is on and armed and reports itself can
    still be doing nothing at all, and the only way to see it is to run
    the same script without it and compare.

    Done here on the files rather than on a machine, which is the point:
    it needs no disk, so it is the one thing about a game session that
    CI can check. It answers what a lone recording never can — that the
    difference the session exists to show is *in* it.
    """
    mine = checkpoints_of(session.path)
    theirs = checkpoints_of(partner.path)
    if not mine or not theirs:
        return "FAIL", "one of the pair carries no checkpoints"
    if [t for t, _ in mine] != [t for t, _ in theirs]:
        # Not the same script, or not the same cadence. Either way the
        # comparison below would be comparing two different runs and
        # would "pass" for the wrong reason.
        return "FAIL", (f"{session.name} and {partner.name} checkpoint at"
                        " different ticks; they are not the same run")

    same = 0
    for (_, a), (_, b) in zip(mine, theirs):
        if a != b:
            break
        same += 1
    if same == len(mine):
        return "FAIL", (f"{session.name} is checkpoint-for-checkpoint"
                        f" identical to {partner.name}: whatever is meant to"
                        " be different made no difference")
    if same == 0:
        return "FAIL", (f"{session.name} and {partner.name} differ at their"
                        " first checkpoint; a pair should share the run up to"
                        " where the one changed thing first matters")
    if mine[-1][1] == theirs[-1][1]:
        return "FAIL", (f"{session.name} and {partner.name} end in the same"
                        " state; the difference did not last")
    return "ok", (f"{same} of {len(mine)} checkpoints identical, then"
                  f" divergent from tick {mine[same][0]} to the end")


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
    """The desktop host to replay with: the **most recently built** one.

    A multi-config generator leaves a host per configuration side by side
    — `hosts/sdl/Debug/` and `hosts/sdl/Release/` — and this used to take
    whichever the glob yielded first, which is `Debug`.  Build only
    `Release` and the sweep then ran a binary that could be days old: it
    reported a divergence against a recording made minutes earlier by the
    build in the tree, and the finding was about neither the machine nor
    the recording.  That is the same failure the session library exists to
    prevent, one layer down — a check that is red, or green, for a reason
    nobody can see.

    Newest wins, and `sweep_report_host()` prints which it was, because a
    rule nobody can check is how the next stale binary gets in.
    """
    found: list[Path] = []
    for tree in trees:
        for name in ("amberfolio", "amberfolio.exe"):
            found += [c for c in tree.glob(f"hosts/sdl/**/{name}")
                      if c.is_file()]
    if not found:
        return None
    return max(found, key=lambda c: c.stat().st_mtime)


def run(command: list[str], env: dict[str, str] | None = None):
    merged = dict(os.environ)
    if env:
        merged.update(env)
    return subprocess.run(
        command, capture_output=True, text=True, env=merged, cwd=ROOT,
        check=False, errors="replace",
    )


def sweep_desktop(host: Path, session: Session, disk: Path,
                  store: Path | None,
                  documents: list[Path]) -> tuple[str, str]:
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
        try:
            command = [str(host), str(copy), str(session.program),
                       "--headless", "--replay", str(session.path)]
            for one in documents:
                # Presented rather than copied: a host hashes what it is
                # handed and never writes to it (`present_document`).
                command += ["--document", str(one)]
            if store is not None:
                # The recording's other input (`Session.store()`). The
                # host reads it whenever it is named, which is what a
                # replay needs it to do — a replay's seams come from the
                # recording, so "the journal seam was asked for" is not a
                # thing this command line says (#235).
                #
                # **Copied, for the same reason the disk is.** A run
                # writes its journal log back into the store when it
                # ends, so replaying over the player's own file would
                # change the file the digest pins and the second sweep of
                # the day would skip. The copy is thrown away with the
                # disk.
                here = Path(work) / "journal-store.txt"
                shutil.copyfile(store, here)
                command += ["--journal-store", str(here)]
            done = run(command, HEADLESS_ENV)
        except OSError as why:
            # A host that will not start is a finding about the build
            # tree, and it belongs in the table beside the others. It
            # used to be a traceback, which reads as the sweep being
            # broken rather than the thing it was pointed at.
            return "FAIL", f"the desktop host would not run: {why}"
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
    parser.add_argument("--targets", default="sdl,ctest,contrast",
                        help="comma-separated: sdl, ctest, contrast"
                             " (default: all three)")
    parser.add_argument("--document", action="append", default=[],
                        metavar="PATH",
                        help="a document a seam is gated on, or a directory "
                             "of them; repeatable, or set "
                             f"{DOCUMENT_ENV}")
    parser.add_argument("--journal-store", action="append", default=[],
                        metavar="PATH",
                        help="a journal store a session may be pinned to, "
                             "or a directory of them; repeatable, or set "
                             f"{STORE_ENV}")
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

    # Journal stores, the same way, and a directory of them counts as
    # all of them: a player who has ingested more than one edition has a
    # folder rather than a list, and the pin is a digest either way.
    given = [Path(p) for p in args.journal_store]
    if not given:
        given = [Path(p) for p
                 in os.environ.get(STORE_ENV, "").split(os.pathsep) if p]
    stores: list[Path] = []
    for one in given:
        stores += sorted(one.glob("*.txt")) if one.is_dir() else [one]

    # And the documents a gated seam needs. A directory of them counts as
    # all of them, because a player who holds two documents has a folder;
    # the pin is a digest either way, so nothing here has to know which
    # file is which.
    shown = [Path(p) for p in args.document]
    if not shown:
        shown = [Path(p) for p
                 in os.environ.get(DOCUMENT_ENV, "").split(os.pathsep) if p]
    held: list[Path] = []
    for one in shown:
        held += sorted(p for p in one.iterdir() if p.is_file()) \
            if one.is_dir() else [one]

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
    # Every session, not the filtered ones: `--session NAME` must still be
    # able to find that one's contrast partner, or asking about half a
    # pair would report the other half missing.
    by_name = {other.name: other for other in find_sessions(None)}
    rows: list[tuple[str, str, str, str]] = []
    failures = 0
    verified = 0
    skipped: list[str] = []
    host_used: Path | None = None

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

        # Needs no disk and no build tree, so it is the one thing about a
        # game session CI can check. Run before the disk work for that
        # reason: it is the row that will still be here when everything
        # else is a skip.
        if "contrast" in wanted and session.descriptor.contrast:
            partner = by_name.get(session.descriptor.contrast)
            if partner is None:
                rows.append((session.name, "contrast", "FAIL",
                             "no session called"
                             f" {session.descriptor.contrast}"))
                failures += 1
            else:
                state, detail = contrast_of(session, partner)
                rows.append((session.name, "contrast", state, detail))
                failures += state == "FAIL"
                verified += state == "ok"

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

        # Before the host, because it is a fact about this session and
        # this machine rather than about the build tree: a descriptor
        # naming a store nobody committed is wrong wherever it is read,
        # and reporting "no host here" instead would hide it on every
        # machine without one.
        store, store_trouble = session.store(stores)
        if store_trouble:
            rows.append((session.name, "sdl", "SKIP", store_trouble))
            skipped.append(session.name)
            continue

        documents, document_trouble = session.documents(held)
        if document_trouble:
            rows.append((session.name, "sdl", "SKIP", document_trouble))
            skipped.append(session.name)
            continue

        host = find_desktop_host(trees)
        if host is None:
            rows.append((session.name, "sdl", "SKIP",
                         "no desktop host in the build tree"))
            skipped.append(session.name)
            continue
        host_used = host
        state, detail = sweep_desktop(host, session, disk, store, documents)
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
    if host_used is not None:
        # Named, for the reason `find_desktop_host` gives: a stale binary
        # beside a fresh one is invisible until somebody says which ran.
        print(f"sweep: desktop host {rel(host_used)}")
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
        # Why nothing verified matters as much as that nothing did, and
        # this line used to say "every check was skipped" whatever the
        # reason — including a run in which every check *failed*, which
        # is the opposite finding and sends somebody looking for a
        # missing disk instead of at a divergence.
        why = ("every check was skipped" if not failures
               else "nothing passed" if skips
               else "every check failed")
        print(f"sweep: NOTHING WAS VERIFIED — {why}."
              " This is not a pass.", file=sys.stderr)

    if failures:
        print("sweep: tests/sessions/README.md says when a session may"
              " legitimately be re-recorded. If none of those changed, a"
              " divergence is a finding about the machine.", file=sys.stderr)
    return 1 if failures or verified == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
