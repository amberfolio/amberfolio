#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Run the visual legs: what a seam draws, and everything it must not.

A **leg** is a key script plus what the screen is allowed to do while it
runs (`tests/visual/*.leg`).  Two kinds, and the difference is not a
detail:

* A **pair** leg runs the script twice, once with the seam off and once
  with it on, and asserts that at every frame it names the two runs
  differ *only* inside the rects the seam owns at that moment — and not
  at all where it names none.  That is `docs/journal-test-plan.md` §3's
  first assertion form, and it catches the class of bug #230 was: the
  program painting its own bar over the journal.

* A **single** leg runs once and asserts that a range of frames is one
  frame.  NOT-8 is the example and the reason the kind exists: "nothing
  reaches the program while the log is up" is a screen holding still,
  not a difference from anything.  It *cannot* be a pair, because the
  seam-off run has no log to swallow those keys, so they reach the
  program and the two runs end in different game states.

    python3 scripts/visual-legs.py --game-disk /path/to/a/copy
    python3 scripts/visual-legs.py --leg rdr-prompt --keep
    AMBERFOLIO_GAME_DISK=/path/to/a/copy python3 scripts/visual-legs.py

Exit status is zero when every leg that could run, ran and passed.

The third outcome, again
------------------------

A leg drives the player's own program, which this repository may not
carry (PLAN.md §6).  So it has the same three outcomes `sweep.py` has,
and the same rule about the third: a run that checked nothing must never
read as a run that passed.

    ok      the leg ran and every frame it names held
    FAIL    it did not, and here is the frame and the box
    SKIP    nothing was checked, and here is what was missing

Nothing it writes is committable
--------------------------------

The stills are pictures of the game running, so they go to a temporary
directory and are deleted (`--keep` to look at them, which is what
`scripts/frames.py sheet` is for).  What a leg *file* holds is a key
script and a list of rectangles, which are facts.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from frames import Rect, difference, digest, load, outside, stills
except ImportError as why:  # pragma: no cover - the message is the handler
    sys.stderr.write(f"visual-legs: {why}\n")
    raise SystemExit(127) from why

ROOT = Path(__file__).resolve().parent.parent
LEGS = ROOT / "tests" / "visual"
GAME_DISK_ENV = "AMBERFOLIO_GAME_DISK"

# The dummy drivers are what make `--press` work with no display, and a
# headless run refuses `--press` outright (`docs/journal-test-plan.md`
# §9). Set here rather than asked of the caller, so a leg run from a
# session on somebody's desk behaves the same as one in a terminal.
DRIVERS = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}

#: Where a leg's frame range may end.
END = "end"


class Leg:
    """One `.leg` file, parsed.

    The format is `sweep.py`'s: one keyword a line, `#` a comment, and a
    line nobody recognizes is a problem rather than a shrug.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.name = path.stem
        self.about: list[str] = []
        self.kind = ""
        self.program = ""
        self.store: str | None = None
        self.seams: list[str] = []
        self.both: list[str] = []
        self.until = 0
        self.every = 25
        self.slack = 0
        self.keys: list[str] = []
        self.rects: dict[str, Rect] = {}
        # (first, last, [rect names]) and (first, last), frames inclusive.
        self.allows: list[tuple[int, int, list[str]]] = []
        self.sames: list[tuple[int, int]] = []
        #: (before, after, [rect names to leave out]): two frames that
        #: must be the same screen, apart from the rects named.
        self.equals: list[tuple[int, int, list[str]]] = []
        self.problems: list[str] = []
        self._read()

    def _frame(self, text: str) -> int:
        if text == END:
            return 1 << 30
        return int(text)

    def _read(self) -> None:
        for number, line in enumerate(
                self.path.read_text(encoding="utf-8").splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            word = line.split()
            try:
                self._one(word)
            except (ValueError, IndexError, KeyError) as why:
                self.problems.append(f"line {number}: {line!r}: {why}")
        if self.kind not in ("pair", "single"):
            self.problems.append("no `kind pair` or `kind single` line")
        if not self.program:
            self.problems.append("no `program` line")
        if not self.until:
            self.problems.append("no `until` line")
        if self.kind == "pair" and not self.allows:
            self.problems.append("a pair leg with no `allow` line checks "
                                 "nothing")
        if self.kind == "single" and not (self.sames or self.equals):
            self.problems.append("a single leg with no `same` or `equal` "
                                 "line checks nothing")
        for first, last, names in (
                self.allows + [(a, b, n) for a, b, n in self.equals]):
            for one in names:
                if one != "none" and one not in self.rects:
                    self.problems.append(
                        f"`allow {first}` names the rect {one!r}, which no "
                        "`rect` line defines")

    def _one(self, word: list[str]) -> None:
        head = word[0]
        if head == "about":
            self.about.append(" ".join(word[1:]))
        elif head == "kind":
            self.kind = word[1]
        elif head == "program":
            self.program = word[1]
        elif head == "store":
            self.store = None if word[1] == "none" else word[1]
        elif head == "seam":
            self.seams.append(word[1])
        elif head == "both":
            self.both.append(word[1])
        elif head == "until":
            self.until = int(word[1])
        elif head == "every":
            self.every = int(word[1])
        elif head == "slack":
            self.slack = int(word[1])
        elif head == "key":
            self.keys.append(word[1])
        elif head == "rect":
            self.rects[word[1]] = Rect.parse(word[2])
        elif head == "allow":
            first, last = word[1].split("-")
            self.allows.append((int(first), self._frame(last), word[2:]))
        elif head == "same":
            first, last = word[1].split("-")
            self.sames.append((int(first), self._frame(last)))
        elif head == "equal":
            self.equals.append((int(word[1]), int(word[2]), word[3:]))
        else:
            raise ValueError(f"no keyword {head!r}")

    def rects_at(self, frame: int) -> list[Rect] | None:
        """The rects allowed at `frame`, or None when no rule covers it.

        The last matching rule wins, so a leg reads top to bottom: a wide
        rule and then the narrower one that overrides part of it.
        """
        found = None
        for first, last, names in self.allows:
            if first <= frame <= last:
                found = [] if names == ["none"] else [self.rects[n]
                                                      for n in names]
        return found


def host_of(build: Path) -> Path | None:
    """The desktop host inside a build tree, whichever way it configured."""
    for name in ("amberfolio", "amberfolio.exe"):
        for where in (build / "hosts" / "sdl",
                      build / "hosts" / "sdl" / "Release",
                      build / "hosts" / "sdl" / "Debug"):
            if (where / name).is_file():
                return where / name
    return None


def run_side(host: Path, disk: Path, leg: Leg, into: Path,
             seams: list[str], store: Path | None) -> tuple[int, str]:
    """One run of a leg's script, dumping into `into`.

    The disk is copied first, because a run writes to it — a save, the
    automap's sidecar — and a leg that mutated the player's own copy
    would be a leg nobody could run twice.
    """
    copy = into / "disk"
    shutil.copytree(disk, copy)
    (into / "stills").mkdir(parents=True, exist_ok=True)
    command = [str(host), str(copy), leg.program,
               "--fast", "max", "--until", str(leg.until),
               "--dump", str(into / "stills" / "f"),
               "--dump-every", str(leg.every)]
    for seam in seams:
        command += ["--seam", seam]
    if store is not None:
        command += ["--journal-store", str(store)]
    for key in leg.keys:
        command += ["--press", key]
    done = subprocess.run(command, capture_output=True, text=True,
                          env={**os.environ, **DRIVERS}, check=False)
    return done.returncode, done.stderr


def frame_of(path: Path) -> int:
    stem = path.stem.rsplit("-", 1)[-1]
    return int(stem) if stem.isdigit() else 1 << 30


def check_pair(leg: Leg, off: Path, on: Path) -> tuple[list[str], str]:
    """Every frame a rule covers: what differs outside what is allowed.

    **`slack` is why a frame is compared against its neighbours too.**
    Claiming a keystroke changes *when* the program does its own next
    repaint, by less than the gap between two dumped stills, so an on
    still can be settled while the off still of the same number is caught
    mid-repaint — the same screen, one dump apart. Measured on the real
    program (#234): over a reader leg exactly one still in hundreds
    differed outside the seam's rects, and the status line's digest was
    equal 25 frames before it and 25 frames after.

    So a frame passes when it is confined against the off run at its own
    number *or* at one within `slack` dumps of it. That says what is
    true — this screen appears in both runs, a moment apart — rather than
    standing back from a range and checking nothing there. A leg with no
    `slack` line compares numbers exactly.
    """
    wrong = []
    index = {frame_of(p): p for p in stills(str(off / "stills"))}
    near = sorted(index)
    checked = 0
    slipped = 0
    for still in stills(str(on / "stills")):
        frame = frame_of(still)
        allowed = leg.rects_at(frame)
        if allowed is None or frame not in index:
            continue
        checked += 1
        mine = load(still)
        at = near.index(frame)
        # Its own number first, so a leg with no slack is the plain
        # comparison and a slack one still reports the nearest failure.
        tries = [frame]
        for step in range(1, leg.slack + 1):
            tries += [near[i] for i in (at - step, at + step)
                      if 0 <= i < len(near)]
        best = None
        for candidate in tries:
            count, box = outside(load(index[candidate]), mine, allowed)
            if count == 0:
                if candidate != frame:
                    slipped += 1
                best = None
                break
            if best is None:
                best = (count, box)
        if best is not None:
            names = ", ".join(str(r) for r in allowed) or "nothing"
            wrong.append(f"frame {frame}: {best[0]} pixels differ outside "
                         f"{names}, box {best[1]}")
    if checked == 0:
        wrong.append("no frame this leg names was dumped by either run")
    return wrong[:8], f"{checked} frames, {slipped} a dump apart"




def check_single(leg: Leg, run: Path) -> tuple[list[str], str]:
    """Every `same` range: one digest, or the first frame that broke it."""
    wrong = []
    found = stills(str(run / "stills"))
    at = {frame_of(p): p for p in found}
    held_total = 0

    # **The give-back, stated without an off run.** "The screen comes back
    # exactly" is two frames of one run being the same screen — the one
    # before the journal took it and the one after it gave it back — and
    # that is both stronger and the only form available. A pair cannot say
    # it: the key that opens the log reaches the program in the seam-off
    # run, where it steps the command bar's highlight, so the two runs
    # differ on the bar from that moment on however exact the give-back is
    # (#234).
    for before, after, names in leg.equals:
        if before not in at or after not in at:
            wrong.append(f"frames {before} and {after}: one of them was not "
                         "dumped")
            continue
        count, box = outside(load(at[before]), load(at[after]),
                             [leg.rects[n] for n in names])
        if count:
            wrong.append(f"frame {after} is not frame {before} again: "
                         f"{count} pixels differ, box {box}")

    for first, last in leg.sames:
        held = [p for p in found if first <= frame_of(p) <= last]
        if len(held) < 2:
            wrong.append(f"frames {first}-{last}: {len(held)} stills, so "
                         "there is nothing to hold still")
            continue
        held_total += len(held)
        want = digest(load(held[0]))
        for path in held[1:]:
            if digest(load(path)) != want:
                count, box = difference(load(held[0]), load(path))
                wrong.append(
                    f"frames {first}-{last}: frame {frame_of(path)} differs "
                    f"from {frame_of(held[0])} by {count} pixels, box {box}")
                break
    note = f"{held_total} frames held still"
    if leg.equals:
        note += f", {len(leg.equals)} given back"
    return wrong, note


def run_leg(leg: Leg, host: Path, disk: Path, keep: Path | None
            ) -> tuple[str, str]:
    if leg.problems:
        return "SKIP", "; ".join(leg.problems)
    store = None
    if leg.store is not None:
        store = ROOT / leg.store
        if not store.is_file():
            return "SKIP", f"no store at {leg.store}"

    work = Path(keep) if keep else Path(tempfile.mkdtemp(prefix="af-leg-"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        on = work / f"{leg.name}-on"
        on.mkdir(parents=True, exist_ok=True)
        code, said = run_side(host, disk, leg, on, leg.both + leg.seams, store)
        if not stills(str(on / "stills")):
            head = said.strip().splitlines()[-1] if said.strip() else ""
            return "FAIL", f"the on run dumped nothing (exit {code}) {head}"

        if leg.kind == "single":
            wrong, note = check_single(leg, on)
        else:
            off = work / f"{leg.name}-off"
            off.mkdir(parents=True, exist_ok=True)
            code, said = run_side(host, disk, leg, off, leg.both, None)
            if not stills(str(off / "stills")):
                head = said.strip().splitlines()[-1] if said.strip() else ""
                return "FAIL", f"the off run dumped nothing (exit {code}) {head}"
            wrong, note = check_pair(leg, off, on)
        if wrong:
            return "FAIL", "; ".join(wrong)
        return "ok", f"{note} - {leg.about[0]}" if leg.about else note
    finally:
        if keep is None:
            shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="visual-legs.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build/windows-msvc",
                        help="a configured build tree with the desktop host")
    parser.add_argument("--game-disk", action="append", default=[],
                        help="a directory holding a copy of the program; "
                             f"or set {GAME_DISK_ENV}")
    parser.add_argument("--leg", help="only this one, by stem")
    parser.add_argument("--keep", metavar="DIR",
                        help="keep the stills here instead of deleting them, "
                             "to look at with scripts/frames.py")
    args = parser.parse_args()

    disks = [Path(p) for p in args.game_disk]
    if not disks and os.environ.get(GAME_DISK_ENV):
        disks = [Path(os.environ[GAME_DISK_ENV])]
    disk = next((d for d in disks if d.is_dir()), None)
    host = host_of(ROOT / args.build)

    found = sorted(LEGS.glob("*.leg"))
    if args.leg:
        found = [p for p in found if p.stem == args.leg]
    if not found:
        sys.stderr.write("visual-legs: no legs found\n")
        return 1

    verdicts = []
    for path in found:
        leg = Leg(path)
        # A leg's own problems first, and before anything about this
        # machine: a leg that names a rect nothing defines is wrong in
        # the tree, and reporting "no disk here" instead would hide it on
        # every machine that has no disk — which is all of CI.
        if leg.problems:
            verdict, why = "SKIP", "; ".join(leg.problems)
        elif host is None:
            verdict, why = "SKIP", f"no desktop host under {args.build}"
        elif disk is None:
            verdict, why = "SKIP", ("no disk; pass --game-disk or set "
                                    f"{GAME_DISK_ENV}")
        else:
            verdict, why = run_leg(leg, host, disk,
                                   Path(args.keep) if args.keep else None)
        verdicts.append(verdict)
        print(f"{verdict:5} {leg.name:22} {why}")

    skipped = verdicts.count("SKIP")
    failed = verdicts.count("FAIL")
    print()
    print(f"{verdicts.count('ok')} ok, {failed} FAILED, {skipped} SKIPPED")
    if skipped == len(verdicts):
        print("visual-legs: NOTHING WAS CHECKED — this is not a pass")
        return 1
    return 1 if failed or skipped else 0


if __name__ == "__main__":
    raise SystemExit(main())
