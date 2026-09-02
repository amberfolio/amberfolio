#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Self-test for scripts/frames.py, the stills tool (#233).
#
# Why this exists. Everything the journal test plan asserts about a
# picture is a number this script printed — a pixel count, a bounding
# box, a digest — and a number nobody checks is a number that drifts. The
# two that would drift silently are the ones with a convention in them:
# a rect is *inclusive* on both sides, unlike PIL's own boxes, and a
# confinement answer is an exit status as well as a line of text. Both
# are asserted below.
#
# The stills here are drawn by this file: three flat colours and a
# rectangle, 320x200 because that is the shape the machine dumps. No
# still of the game is committed, produced or needed, which is the
# content rule as `docs/journal-test-plan.md` §3 applies it to pictures.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
python=${PYTHON:-python3}

frames() { # frames <args...> -- runs the tool, keeping both halves of the answer
  code=0
  out=$("$python" "$here/frames.py" "$@" 2>&1) || code=$?
}

expect_code() { # expect_code <description> <wanted>
  if [ "$code" -eq "$2" ]; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (wanted exit $2, got $code)"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  fi
}

expect_says() { # expect_says <description> <text>
  if printf '%s' "$out" | grep -qF -- "$2"; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (nothing said '$2')"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  fi
}

expect_silent_about() { # expect_silent_about <description> <text>
  if printf '%s' "$out" | grep -qF -- "$2"; then
    echo "FAIL: $1 (it said '$2' and should not have)"
    printf '%s\n' "$out" | sed 's/^/    /'
    fail=1
  else
    echo "ok: $1"
  fi
}

expect_file() { # expect_file <description> <path>
  if [ -s "$2" ]; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (no file at $2, or it is empty)"
    fail=1
  fi
}

# --- the stills -----------------------------------------------------------
#
# Two runs of four frames each, standing in for a seam-off and a seam-on
# pair. They are identical at frames 0 and 100; from 200 the "on" run has
# a block inside the rect 136,8..311,119, which is the reader panel's
# geometry and so the shape a real confinement leg is written against.
"$python" - "$tmp" <<'PY'
import sys
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(sys.argv[1])
for run in ("off", "on"):
    (root / run).mkdir(parents=True, exist_ok=True)
    for frame in (0, 100, 200, 300):
        image = Image.new("RGB", (320, 200), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        # Something that changes between frames in *both* runs, so that
        # "changed" has work to do that is not the seam's doing.
        draw.rectangle([0, 190, 40, 199], fill=(0, frame % 256, 0))
        if run == "on" and frame >= 200:
            # Inside the panel rect, inclusive 136,8..311,119.
            draw.rectangle([136, 8, 311, 119], fill=(255, 255, 0))
        if run == "on" and frame == 300:
            # And one pixel outside it, which is the case a confinement
            # leg exists to catch.
            draw.point((312, 150), fill=(255, 0, 0))
        image.save(root / run / f"f-{frame:06d}.ppm")
PY
echo "ok: drew 8 stills, 320x200"

# --- png ------------------------------------------------------------------
frames png "$tmp/on/f-000000.ppm" --scale 3
expect_code "png converts a still" 0
expect_file "png wrote a PNG beside it" "$tmp/on/f-000000.png"
"$python" - "$tmp/on/f-000000.png" <<'PY'
import sys
from PIL import Image
w, h = Image.open(sys.argv[1]).size
raise SystemExit(0 if (w, h) == (960, 600) else f"got {w}x{h}")
PY
echo "ok: png scaled 320x200 by three"

frames png "$tmp/on" --scale 1 --out "$tmp/allpng"
expect_code "png takes a whole run" 0
expect_file "png wrote every frame" "$tmp/allpng/f-000300.png"

# --- crop, and the inclusive-rect convention ------------------------------
#
# The rule this pins: 136,8,311,119 is 176 by 112. Written half-open it
# would be 175 by 111, and every rect in the plan's §3 table would be a
# pixel short on two sides.
frames crop "$tmp/on/f-000200.ppm" --rect 136,8,311,119 --out "$tmp/panel.png"
expect_code "crop takes a rect" 0
expect_says "a rect is inclusive on both sides" "176x112"
expect_file "crop wrote the rect" "$tmp/panel.png"

frames crop "$tmp/on/f-000200.ppm" --rect 0,0,999,999 --out "$tmp/no.png"
expect_code "a rect bigger than the still is refused" 1
expect_says "and says what the still is" "320x200"

frames crop "$tmp/on/f-000200.ppm" --rect 10,10,5,5 --out "$tmp/no.png"
expect_code "an inside-out rect is refused" 1
expect_says "and says so" "inside out"

frames crop "$tmp/on/f-000200.ppm" --rect 1,2,3 --out "$tmp/no.png"
expect_code "a rect that is not four numbers is refused" 1
expect_says "and says what one looks like" "x0,y0,x1,y1"

# --- hash -----------------------------------------------------------------
same_a=$("$python" "$here/frames.py" hash "$tmp/on/f-000200.ppm" | cut -d' ' -f1)
same_b=$("$python" "$here/frames.py" hash "$tmp/on/f-000200.ppm" | cut -d' ' -f1)
if [ "$same_a" = "$same_b" ]; then
  echo "ok: the same still hashes the same twice"
else
  echo "FAIL: the same still hashed two ways"
  fail=1
fi

other=$("$python" "$here/frames.py" hash "$tmp/off/f-000200.ppm" | cut -d' ' -f1)
if [ "$same_a" != "$other" ]; then
  echo "ok: two different stills hash differently"
else
  echo "FAIL: two different stills hashed the same"
  fail=1
fi

# The panel rect is where the two runs differ, so its digest separates
# them; the bar rect is identical in both, so its digest cannot. That
# pair is the whole point of hashing a rect rather than a frame.
panel_on=$("$python" "$here/frames.py" hash "$tmp/on/f-000200.ppm" \
  --rect 136,8,311,119 | cut -d' ' -f1)
panel_off=$("$python" "$here/frames.py" hash "$tmp/off/f-000200.ppm" \
  --rect 136,8,311,119 | cut -d' ' -f1)
bar_on=$("$python" "$here/frames.py" hash "$tmp/on/f-000200.ppm" \
  --rect 0,190,40,199 | cut -d' ' -f1)
bar_off=$("$python" "$here/frames.py" hash "$tmp/off/f-000200.ppm" \
  --rect 0,190,40,199 | cut -d' ' -f1)
if [ "$panel_on" != "$panel_off" ] && [ "$bar_on" = "$bar_off" ]; then
  echo "ok: a rect's digest answers about that rect and no other"
else
  echo "FAIL: rect digests did not separate the panel from the bar"
  fail=1
fi

frames hash "$tmp/nothing-here"
expect_code "hashing what is not there fails" 1

# --- diff -----------------------------------------------------------------
frames diff "$tmp/off/f-000000.ppm" "$tmp/on/f-000000.ppm"
expect_code "two identical stills exit zero" 0
expect_says "and say so" "same"

frames diff "$tmp/off/f-000200.ppm" "$tmp/on/f-000200.ppm"
expect_code "two differing stills exit one" 1
expect_says "and print the box, inclusive" "box 136,8,311,119"
expect_says "and how many pixels differ" "19712 pixels differ"

frames diff "$tmp/off/f-000200.ppm" "$tmp/on/f-000200.ppm" \
  --allow 136,8,311,119
expect_code "a difference inside an allowed rect exits zero" 0
expect_says "and says it is confined" "confined to 136,8,311,119"

frames diff "$tmp/off/f-000300.ppm" "$tmp/on/f-000300.ppm" \
  --allow 136,8,311,119
expect_code "one pixel outside the allowed rect exits two" 2
expect_says "and says where the stray pixel is" "box 312,150,312,150"
expect_says "and how many there were" "1 pixels differ outside"

# Exit two and not one, because a leg has three answers and not two:
# nothing changed, the seam changed what it owns, and the seam changed
# something else. Collapsing the last two is the bug #230 was.
frames diff "$tmp/off/f-000000.ppm" "$tmp/on/f-000000.ppm" \
  --allow 136,8,311,119
expect_code "no difference at all is still zero with --allow" 0

# The case that makes confinement a *masking* question and not a
# bounding-box one, and the reason the check was rewritten in #233: a
# seam owning two rects far apart produces one box spanning both, and
# that box is inside neither. On the real program every frame of a
# reader leg has this shape — the panel near the top, `Notes` on the bar
# at the bottom — so a check that compared boxes would call the correct
# behaviour a failure and nothing would ever pass.
frames diff "$tmp/off/f-000300.ppm" "$tmp/on/f-000300.ppm"
expect_says "two distant changes make one box spanning both" \
  "box 136,8,312,150"
frames diff "$tmp/off/f-000300.ppm" "$tmp/on/f-000300.ppm" \
  --allow 136,8,311,119 --allow 312,150,312,150
expect_code "two disjoint allowed rects are both honoured" 0
expect_says "and both are named" "312,150,312,150"

# And the converse, so that --allow cannot pass by being generous: a rect
# that covers neither change is not an excuse for either.
frames diff "$tmp/off/f-000300.ppm" "$tmp/on/f-000300.ppm" \
  --allow 0,0,10,10
expect_code "an allowed rect that covers nothing excuses nothing" 2

frames diff "$tmp/off/f-000200.ppm" "$tmp/on/f-000200.ppm" \
  --rect 0,190,40,199
expect_code "a diff over a rect ignores everything outside it" 0

# --- changed --------------------------------------------------------------
frames changed "$tmp/on"
expect_code "changed walks a run" 0
expect_says "and names the first still always" "f-000000.ppm"
expect_says "and the frame the block appeared on" "f-000200.ppm"

# Over the panel rect alone, frames 0 and 100 are one still and 200 is
# the change; frame 300's extra pixel is outside the rect, so it is not.
frames changed "$tmp/on" --rect 136,8,311,119
expect_code "changed takes a rect" 0
expect_says "the panel changed at 200" "f-000200.ppm"
expect_silent_about "and not at 100" "f-000100.ppm"
expect_silent_about "and not at 300" "f-000300.ppm"

frames changed "$tmp/empty-run"
expect_code "changed on nothing fails" 1
expect_says "and says where it looked" "empty-run"

# --- sheet ----------------------------------------------------------------
frames sheet "$tmp/on" --out "$tmp/sheet-one.png"
expect_code "a sheet of one run's changes" 0
expect_file "wrote the sheet" "$tmp/sheet-one.png"

frames sheet "$tmp/on" --against "$tmp/off" --out "$tmp/sheet-pair.png"
expect_code "a sheet of where two runs differ" 0
expect_file "wrote the pair sheet" "$tmp/sheet-pair.png"
expect_says "it names the frames that differ" "000200"
expect_silent_about "and not the frames that agree" "  000000"

# A run holds one screen for many stills, so consecutive tiles showing
# the same thing collapse into one labelled with the range it stands for.
# Over the panel rect the "on" run is two screens — empty at 0 and 100,
# the block at 200 and 300 — so its sheet is two tiles and not four.
# Without this a real leg's sheet is forty pictures of one screen.
frames sheet "$tmp/on" --rect 136,8,311,119 --out "$tmp/sheet-span.png"
expect_code "a sheet collapses repeated screens" 0
expect_says "and labels the range one tile stands for" "000000..000100"
expect_says "and the next range too" "000200..000300"
expect_says "so four stills are two tiles" "2 frames"

frames sheet "$tmp/off" --against "$tmp/off" --out "$tmp/sheet-none.png"
expect_code "a run against itself is not a sheet" 0
expect_says "and says nothing changed" "nothing changed"
if [ -e "$tmp/sheet-none.png" ]; then
  echo "FAIL: it wrote a sheet of nothing"
  fail=1
else
  echo "ok: and wrote no file"
fi

frames sheet "$tmp/on" --against "$tmp/no-such-run" --out "$tmp/no.png"
expect_code "a sheet against nothing fails" 1

echo
if [ "$fail" -eq 0 ]; then
  echo "test-frames: OK"
else
  echo "test-frames: FAILED"
fi
exit "$fail"
