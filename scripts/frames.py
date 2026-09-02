#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Look at, measure and compare the stills a run of the machine dumped.

``--dump PREFIX --dump-every N`` on either host writes one PPM per N
frames (``docs/hosts.md``).  This is the tool for the questions a person
then asks of them: *what does that look like*, *what changed*, *did
anything change outside the rect it was allowed to*, and *is this frame
the same frame as that one*.

    python3 scripts/frames.py png       on/f-011000.ppm
    python3 scripts/frames.py crop      on/f-011000.ppm --rect 136,8,311,119
    python3 scripts/frames.py hash      on/f-*.ppm --rect 136,8,311,119
    python3 scripts/frames.py diff      off/f-011000.ppm on/f-011000.ppm
    python3 scripts/frames.py changed   on
    python3 scripts/frames.py sheet     on --against off --out sheet.png

Nothing it writes may be committed
----------------------------------

Every still here is a picture of the player's own program running, so a
PNG this makes is game content in the sense CONTRIBUTING.md means and the
content guard would refuse it (`docs/journal-test-plan.md` §3).  The tool
therefore writes beside its input by default — a scratch directory, never
the tree — and this file's own self-test (`scripts/test-frames.sh`) works
on stills it draws itself.  What may be committed is what this prints:
counts, boxes and digests are facts.

Rects are inclusive, on both sides
----------------------------------

``--rect x0,y0,x1,y1`` names the pixel columns x0 *through* x1 and the
rows y0 *through* y1, and a box this prints is in the same form.  So the
reader panel is ``136,8,311,119`` and is 176 by 112, which is how
`docs/journal-test-plan.md` §3's table writes it.

That is deliberately *not* PIL's convention — ``Image.getbbox()`` returns
a half-open box whose right and lower edges are one past the last pixel —
and the one place it matters is reading an older measurement: the
``(136, 8, 312, 199)`` recorded in the plan's §9 is PIL's, and is
``136,8,311,198`` here.  One convention throughout beats two, and the one
that matches the rects a person writes in a leg file is the one to keep.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw
except ImportError:  # pragma: no cover - the message is the whole handler
    sys.stderr.write(
        "frames: this needs Pillow.\n"
        "  pip install pillow\n"
    )
    raise SystemExit(127)


# A still the machine dumps is 320x200 and the hosts upscale it; three is
# what makes a 320x200 frame comfortable to read on a modern display
# without resampling a single pixel into a smear.
DEFAULT_SCALE = 3


class Rect:
    """An inclusive rectangle of pixels, and the crop it stands for."""

    def __init__(self, x0: int, y0: int, x1: int, y1: int) -> None:
        if x1 < x0 or y1 < y0:
            raise ValueError(f"rect {x0},{y0},{x1},{y1} is inside out")
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

    @classmethod
    def parse(cls, text: str) -> "Rect":
        parts = text.split(",")
        if len(parts) != 4:
            raise ValueError(f"rect '{text}' wants four numbers: x0,y0,x1,y1")
        try:
            return cls(*(int(p) for p in parts))
        except ValueError as why:
            raise ValueError(f"rect '{text}': {why}") from why

    @property
    def box(self) -> tuple[int, int, int, int]:
        """The same rectangle in PIL's half-open form."""
        return (self.x0, self.y0, self.x1 + 1, self.y1 + 1)

    @property
    def width(self) -> int:
        return self.x1 - self.x0 + 1

    @property
    def height(self) -> int:
        return self.y1 - self.y0 + 1

    def contains(self, other: "Rect") -> bool:
        return (other.x0 >= self.x0 and other.y0 >= self.y0
                and other.x1 <= self.x1 and other.y1 <= self.y1)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Rect) and str(self) == str(other)

    def __str__(self) -> str:
        return f"{self.x0},{self.y0},{self.x1},{self.y1}"


def load(path: Path, rect: Rect | None = None) -> Image.Image:
    """One still, as RGB, cropped if asked.

    Converted rather than trusted: a host writes P6 today, and a reader
    that assumed the mode would break silently on the day one writes
    anything else.
    """
    image = Image.open(path).convert("RGB")
    if rect is None:
        return image
    if rect.x1 >= image.width or rect.y1 >= image.height:
        raise ValueError(
            f"{path.name} is {image.width}x{image.height}, "
            f"which does not hold rect {rect}"
        )
    return image.crop(rect.box)


def difference(a: Image.Image, b: Image.Image) -> tuple[int, Rect | None]:
    """How many pixels differ, and the box that holds all of them.

    Both, because neither answers the other's question: a confinement leg
    wants the box, and "did anything at all change" wants the count. A
    box with a count of zero is impossible; a count with no box is the
    bug that would let an empty diff pass as a confined one.
    """
    if a.size != b.size:
        raise ValueError(f"{a.size} and {b.size} are not the same picture")
    delta = ImageChops.difference(a, b)
    box = delta.getbbox()
    if box is None:
        return 0, None
    # The brightest of the three channels per pixel, then its histogram:
    # a pixel differs if any channel does, and a histogram counts in C.
    # Walking the pixels in Python was the obvious way and is a hundred
    # times slower over the thousand stills a `--dump-every 25` run
    # leaves.
    red, green, blue = delta.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    count = sum(mask.histogram()[1:])
    return count, Rect(box[0], box[1], box[2] - 1, box[3] - 1)


def outside(a: Image.Image, b: Image.Image,
            allowed: list[Rect]) -> tuple[int, Rect | None]:
    """What differs between two stills *outside* the rects allowed.

    The rects are blanked in the difference and what survives is the
    answer, rather than asking whether the whole difference's bounding
    box fits inside one of them. That distinction is the whole check, and
    getting it wrong makes it vacuous: a seam that owns two disjoint
    rects — the reader panel high on the screen and the `Notes` cells on
    the bar at the bottom — produces one bounding box spanning both, and
    that box is inside neither. Measured on the real program, every frame
    of a reader leg has exactly that shape (#233).
    """
    if a.size != b.size:
        raise ValueError(f"{a.size} and {b.size} are not the same picture")
    delta = ImageChops.difference(a, b)
    for rect in allowed:
        delta.paste((0, 0, 0), rect.box)
    box = delta.getbbox()
    if box is None:
        return 0, None
    red, green, blue = delta.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    return sum(mask.histogram()[1:]), Rect(box[0], box[1],
                                           box[2] - 1, box[3] - 1)


def digest(image: Image.Image) -> str:
    """The SHA-256 of a still's pixels.

    Of the *pixels* and not the file, so that a rect's digest and a whole
    frame's are the same kind of fact, and so that a host that changed its
    PPM header would not change every hash in a leg file.
    """
    sha = hashlib.sha256()
    sha.update(f"{image.width}x{image.height}\n".encode("ascii"))
    sha.update(image.tobytes())
    return sha.hexdigest()


#: What `frame_number()` answers for the one still with no number in its
#: name — the frame a run dumps as it stops, which `--dump PREFIX` writes
#: as `PREFIX.ppm` beside the numbered ones.
FINAL = "final"


def stills(where: str) -> list[Path]:
    """The stills a `--dump PREFIX` run left, in frame order.

    `where` is a directory, a prefix, or a glob. A run dumped with
    `--dump on/f` fills `on/` with `f-000000.ppm` upwards and one `f.ppm`
    for the frame it stopped on, so all three spellings of that run are
    ones a person will type.

    Sorted by frame number rather than by name, with the final still
    last: frame 900 sorts before frame 1000 either way while the host
    pads to six digits, and this does not depend on that.
    """
    path = Path(where)
    if path.is_dir():
        found = list(path.glob("*.ppm"))
    else:
        found = [Path(p) for p in glob.glob(where)]
        if not found:
            found = [Path(p) for p in glob.glob(f"{where}*.ppm")]
    return sorted(found, key=lambda p: (frame_number(p) == FINAL,
                                        _numeric(frame_number(p)),
                                        p.name))


def _numeric(frame: str) -> int:
    return int(frame) if frame.isdigit() else 0


def frame_number(path: Path) -> str:
    """The frame a still is of, as the host spelled it in the name."""
    tail = path.stem.rsplit("-", 1)[-1]
    return tail if tail.isdigit() else FINAL


def pair_up(one: list[Path], other: list[Path]) -> list[tuple[Path, Path]]:
    """Stills of the same frame in two runs, by frame number.

    By number rather than by position, because two runs that dumped a
    different number of stills are exactly the case where lining them up
    by index compares frame 11,000 against frame 11,100 and reports a
    difference that is only the clock.
    """
    index = {frame_number(p): p for p in other}
    return [(p, index[frame_number(p)]) for p in one
            if frame_number(p) in index]


# ---------------------------------------------------------------------------
# The subcommands
# ---------------------------------------------------------------------------


def cmd_png(args: argparse.Namespace) -> int:
    """Stills as PNGs, upscaled, for a person to look at."""
    rect = Rect.parse(args.rect) if args.rect else None
    written = 0
    for name in args.stills:
        for path in stills(name) or [Path(name)]:
            image = load(path, rect)
            if args.scale != 1:
                image = image.resize(
                    (image.width * args.scale, image.height * args.scale),
                    Image.NEAREST,
                )
            out = Path(args.out or path.parent) / (path.stem + ".png")
            out.parent.mkdir(parents=True, exist_ok=True)
            image.save(out)
            print(out)
            written += 1
    if written == 0:
        sys.stderr.write("frames: nothing to convert\n")
        return 1
    return 0


def cmd_crop(args: argparse.Namespace) -> int:
    """One rect of one still, saved on its own."""
    rect = Rect.parse(args.rect)
    image = load(Path(args.still), rect)
    out = Path(args.out) if args.out else Path(args.still).with_suffix(".crop.png")
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out)
    print(f"{out} {rect.width}x{rect.height}")
    return 0


def cmd_hash(args: argparse.Namespace) -> int:
    """The digest of each still, or of one rect of each."""
    rect = Rect.parse(args.rect) if args.rect else None
    found = [p for name in args.stills for p in (stills(name) or [Path(name)])]
    if not found:
        sys.stderr.write("frames: nothing to hash\n")
        return 1
    for path in found:
        print(f"{digest(load(path, rect))}  {path}")
    return 0


def cmd_diff(args: argparse.Namespace) -> int:
    """Two stills: how many pixels differ, and the box holding them.

    Exit status is the answer as well as the print, so a leg can branch
    on it: zero when nothing differs, one when something does.
    """
    rect = Rect.parse(args.rect) if args.rect else None
    a = load(Path(args.a), rect)
    b = load(Path(args.b), rect)
    count, box = difference(a, b)
    if count == 0:
        print("same")
        return 0
    where = "" if rect is None else f" (inside {rect})"
    print(f"{count} pixels differ, box {box}{where}")
    if not args.allow:
        return 1

    allowed = [Rect.parse(one) for one in args.allow]
    left, left_box = outside(a, b, allowed)
    if left == 0:
        print("confined to " + ", ".join(str(one) for one in allowed))
        return 0
    print(f"NOT confined: {left} pixels differ outside, box {left_box}")
    print("  allowed: " + ", ".join(str(one) for one in allowed))
    return 2


def cmd_changed(args: argparse.Namespace) -> int:
    """The stills in a run that differ from the one before them.

    A run dumping every 25 frames leaves a thousand files of which a
    dozen are distinct; this is the list a person actually wants, and the
    input to a contact sheet.
    """
    rect = Rect.parse(args.rect) if args.rect else None
    found = stills(args.run)
    if not found:
        sys.stderr.write(f"frames: no stills under '{args.run}'\n")
        return 1
    previous = None
    for path in found:
        current = digest(load(path, rect))
        if current != previous:
            print(f"{path} {current[:8]}")
        previous = current
    return 0


def cmd_sheet(args: argparse.Namespace) -> int:
    """A contact sheet of what changed, for the one visual pass a person makes.

    With `--against`, the frames where two runs differ — the seam-off and
    seam-on pair of a confinement leg, where what a person wants to see
    is every frame the seam touched and nothing else. Without it, the
    frames where the run changed from its own previous still.
    """
    rect = Rect.parse(args.rect) if args.rect else None
    found = stills(args.run)
    if not found:
        sys.stderr.write(f"frames: no stills under '{args.run}'\n")
        return 1

    # (still, what it shows, an extra note for its label). The middle one
    # is what consecutive frames are collapsed on below.
    seen: list[tuple[Path, str, str]] = []
    if args.against:
        other = stills(args.against)
        if not other:
            sys.stderr.write(f"frames: no stills under '{args.against}'\n")
            return 1
        pairs = pair_up(found, other)
        if not pairs:
            sys.stderr.write("frames: the two runs share no frame numbers\n")
            return 1
        for mine, theirs in pairs:
            image = load(mine, rect)
            count, box = difference(image, load(theirs, rect))
            if count:
                seen.append((mine, digest(image), str(box)))
    else:
        for path in found:
            seen.append((path, digest(load(path, rect)), ""))

    # A run dumping every 25 frames holds each screen for dozens of
    # stills, and a sheet with forty tiles of one screen is a sheet
    # nobody reads. Consecutive stills showing the same thing become one
    # tile, labelled with the range of frames it stands for.
    picked: list[tuple[Path, str]] = []
    spans: list[list[str]] = []
    notes: list[str] = []
    previous = None
    for path, what, note in seen:
        if what == previous:
            spans[-1][1] = frame_number(path)
            continue
        picked.append((path, ""))
        spans.append([frame_number(path), frame_number(path)])
        notes.append(note)
        previous = what
    for index, (first, last) in enumerate(spans):
        where = first if first == last else f"{first}..{last}"
        picked[index] = (picked[index][0],
                         f"{where} {notes[index]}".strip())

    if not picked:
        print("nothing changed; no sheet written")
        return 0
    if len(picked) > args.limit:
        print(f"{len(picked)} distinct frames; showing the first {args.limit}")
        picked = picked[: args.limit]

    tile = load(picked[0][0], rect)
    scale = args.scale
    cell_w, cell_h = tile.width * scale, tile.height * scale
    label_h = 12
    columns = min(args.columns, len(picked))
    rows = (len(picked) + columns - 1) // columns
    pad = 4

    sheet = Image.new(
        "RGB",
        (columns * (cell_w + pad) + pad,
         rows * (cell_h + label_h + pad) + pad),
        (24, 24, 24),
    )
    draw = ImageDraw.Draw(sheet)
    for index, (path, label) in enumerate(picked):
        column, row = index % columns, index // columns
        x = pad + column * (cell_w + pad)
        y = pad + row * (cell_h + label_h + pad)
        image = load(path, rect)
        if scale != 1:
            image = image.resize((cell_w, cell_h), Image.NEAREST)
        sheet.paste(image, (x, y))
        draw.text((x, y + cell_h + 1), label, fill=(200, 200, 200))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"{out} {len(picked)} frames, {columns}x{rows}")
    for path, label in picked:
        print(f"  {label}  {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="frames.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Rects are inclusive: x0,y0,x1,y1. Nothing this writes may "
               "be committed (docs/journal-test-plan.md §3).",
    )
    sub = parser.add_subparsers(dest="what", required=True)

    p = sub.add_parser("png", help="stills as upscaled PNGs, to look at")
    p.add_argument("stills", nargs="+")
    p.add_argument("--rect", help="only this rect, inclusive x0,y0,x1,y1")
    p.add_argument("--scale", type=int, default=DEFAULT_SCALE)
    p.add_argument("--out", help="directory (default: beside each still)")
    p.set_defaults(handler=cmd_png)

    p = sub.add_parser("crop", help="one rect of one still")
    p.add_argument("still")
    p.add_argument("--rect", required=True)
    p.add_argument("--out")
    p.set_defaults(handler=cmd_crop)

    p = sub.add_parser("hash", help="SHA-256 of a still, or of a rect of it")
    p.add_argument("stills", nargs="+")
    p.add_argument("--rect")
    p.set_defaults(handler=cmd_hash)

    p = sub.add_parser("diff", help="two stills: pixels differing, and the box")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--rect", help="compare only this rect")
    p.add_argument("--allow", action="append", default=[],
                   help="a rect the seam owns here; repeatable. What differs "
                        "outside every one of them is the answer: exit 0 when "
                        "nothing does and 2 when something does")
    p.set_defaults(handler=cmd_diff)

    p = sub.add_parser("changed", help="the stills in a run that changed")
    p.add_argument("run", help="a directory, a --dump prefix, or a glob")
    p.add_argument("--rect")
    p.set_defaults(handler=cmd_changed)

    p = sub.add_parser("sheet", help="a contact sheet of what changed")
    p.add_argument("run")
    p.add_argument("--against", help="another run; sheet where the two differ")
    p.add_argument("--rect")
    p.add_argument("--out", required=True)
    p.add_argument("--scale", type=int, default=2)
    p.add_argument("--columns", type=int, default=4)
    p.add_argument("--limit", type=int, default=64,
                   help="stop after this many frames (default 64)")
    p.set_defaults(handler=cmd_sheet)

    args = parser.parse_args()
    try:
        return args.handler(args)
    except (ValueError, OSError) as why:
        sys.stderr.write(f"frames: {why}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
