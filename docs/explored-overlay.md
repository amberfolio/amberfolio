# The explored overlay: the facts and the three decisions

*What the overworld screen is, in addresses and offsets; the pixel
geometry measured off a real frame rather than derived; the keystrokes
that put a party on that screen; and the marking, the redraw point and
the record shape decided with what each of them rejects. M5-E5a (#253) of
#179, PLAN.md §5 item 5, and step 1 of `docs/seams.md` §8.3 for the one
enhancement in the plan with no proven prior design.*

Nothing here is seam code. This is the fact table M5-E5b (#254) and
M5-E5c (#255) build from, and it is written down rather than carried in a
head because §8.1 says every address needs the method that found it *and*
an independent check, and because the three decisions below are the part
of this enhancement that nobody has made before.

Everything below is an address, an offset, a length, a piece of
arithmetic or a measurement. No game code, data, text or byte sequence
appears here or in anything this document leads to (CONTRIBUTING.md).

1. [What the overworld screen is](#1-what-the-overworld-screen-is)
2. [The fact table, each fact twice](#2-the-fact-table-each-fact-twice)
3. [The geometry, measured](#3-the-geometry-measured)
4. [The recipe that reaches the screen](#4-the-recipe-that-reaches-the-screen)
5. [The marking, decided](#5-the-marking-decided)
6. [The redraw point, decided](#6-the-redraw-point-decided)
7. [The record's shape, decided](#7-the-records-shape-decided)
8. [What this does not settle](#8-what-this-does-not-settle)

---

## 1. What the overworld screen is

It is the program's **wilderness travel view**, and it is not the
sixteen-by-sixteen grid the automap panel is a map of (#173).

The party walks one of three wilderness areas. An area is **sixteen
columns by thirty-six rows** — 576 cells — and the screen shows a
**five-by-five window** of overhead tiles that scrolls with the party,
with the party's own icon drawn in one cell of it. The program is on that
screen when the game-mode byte is 3 and the view kind is 2, 3 or 4, one
value per area.

Two things follow, and both are why #179 is work rather than a switch:

* **Nothing records wilderness exploration today.** The automap seam is
  gated on mode 4 and view kind 1 and never runs here, and neither did
  the design it carries. The store and the host callout are #173's and
  are reused; the recording and the record are new.
* **The party's overland position is not the automap's position.** It is
  two words in the area record, not the two data-segment bytes the
  automap and `docs/playable.md`'s watch list read. On the overland those
  two bytes hold something else entirely — measured below.

The whole screen is composed off-screen into a back buffer and then
**presented** by a resident routine that flushes only the scanlines
something dirtied. So the place to draw is the *return* of that present,
which §6 argues.

---

## 2. The fact table, each fact twice

Every row carries the route that found it and the independent route that
agreed (`docs/seams.md` §8.1). "Driven" means a real run of a player's
own copy under the recipe in §4.

### The screen, in the data segment

| what | where | second route |
| --- | --- | --- |
| game mode; **3** is the travel view | `0x49F3` | already in this tree three times (`seam_cheats.cpp`, `seam_encamp_fix.cpp`, `seam_automap.cpp`); driven, it becomes 3 as the party arrives |
| view kind; **2, 3, 4** are the three wilderness areas | `0x49FA` | the redraw's own test is `> 1 && < 5`; driven, it is 2 on the area reached in §4 |
| a scripted move is in flight | `0x442F` | `seam_automap.cpp` reads the same byte for the same reason |
| the disk the area's files come from | `0x5376` | driven, 6 on the area reached in §4 |
| the area id | `0x84DC` | driven, `0x19` on that area, which is the id the automap's own zone table already carries for it |
| the party's facing, 0 N / 2 E / 4 S / 6 W | `0x6AAF` | `docs/playable.md`'s watch list; driven, 4 with the status line reading `S` |
| the column-bias table, indexed by the view kind | `0x3C76` | it begins exactly where the terrain table below ends (`0x3648 + 0x2C * 36 = 0x3C78`), and its three live entries — 0, 13 and 26 for kinds 2, 3 and 4 — are the two constants the program's own status line adds to the column it prints for kinds 3 and 4 |
| the overland terrain table: 36 rows, stride `0x2C`, the three areas being three sixteen-column bands of it at those biases | `0x3648` | the move step and the window painter index it with the same arithmetic, written independently of each other |

**A seam reads the bias out of the program rather than carrying it.** The
three values above are the check, not the implementation.

### The area record, through the far pointer at `0x49D2`

`0x49D2` is a **far pointer** — offset then segment — and not a buffer at
that offset. This tree has read through it twice already
(`seam_encamp_fix.cpp` for the game clock, `seam_automap.cpp` for the
geometry block).

| what | offset in the record | second route |
| --- | --- | --- |
| the party's overland **column**, a word, clamped 0..15 by the move step | `+0x186` | two routines read it independently — the move step and the status line — and the status line **prints it**: driven, the screen read `3, 32` at the moment the record held 3 and 32 |
| the party's overland **row**, a word, clamped 0..35 | `+0x188` | the same two routines, and the same printed line |
| non-zero means the program draws these areas in **3D** instead, whatever the view kind says | `+0x1CC` | the redraw consults it before anything else and forces the interior path; it is the one guard a mode-and-kind test alone would miss (§6) |

**The two bytes at `0x6AAD`/`0x6AAE` are not the overland position**, and
this is the fact most likely to be assumed wrong, because they *are* the
position everywhere else. Driven, with the status line reading `3, 32`,
those two bytes held `0x0B` and `0x0D`. They are the interior grid's
cell and mean nothing here.

### The routines

Offsets are from the image base, by the arithmetic this tree already
relies on — a decompiled `seg:off` with a load segment of `0x1000` maps
to `(seg - 0x1000) * 16 + off`, which is confirmed by three addresses
this build already executes against (the code wheel's compare, the encamp
fix's region clear, and the damage routine the call door was anchored
on).

| routine | where | what it is | second route |
| --- | --- | --- | --- |
| the back-buffer **present** | image `0x6192` | flushes the scanlines something dirtied: composes into the second display page a plane at a time and then latch-copies that span to the visible page | disassembled at its address it opens `push bp / mov bp, sp` and immediately reads the six data-segment words the theory says it must — the adapter byte, the back-buffer far pointer, the per-row dirty flags, the per-row minimum and maximum x and the per-row destination x |
| its **return** | image `0x649B` | `mov sp, bp / pop bp / retf`, the only far return in the routine | the same disassembly, read to the end |
| the per-mode **composer** | overlay 26 rel `0x016D` | picks the interior path or the travel path and, on the travel path, computes the window's top-left and calls the two painters | its arithmetic is reproduced by the icon painter's own band computation, which is a different routine reaching the same answer |
| the five-by-five **window painter** | overlay 27 rel `0x0000`, `retf 4`, arguments row then column | twenty-five cells, the cell index stepping by one and the blit position by three | the measurement in §3 |
| the per-cell **blit thunk** | image `0x3EBA` into overlay 32 rel `0x00F1`, `retf 8` | one overhead tile into the back buffer | — |
| the party **icon painter** | overlay 26 rel `0x0000`, `retf 0` | draws the icon in the party's cell of the window and presents | — |

The overlay rows, from the overlay file's own table: 26 at file offset
`0x02DB87` length `0x2A5`; 27 at `0x02DE38` length `0x288`; 32 at
`0x031A6C` length `0x645`. **No point this enhancement needs is in an
overlay** — see §6 — so those are context rather than facts a seam
carries.

### The window's arithmetic

The composer computes the window's top-left cell as

    col = bias(view kind) + column - 2, clamped to [0, 0x27]
    row = row - 2,                      clamped to [0, 0x1F]

and the icon painter, separately, computes which of the five bands the
party is in — 0 or 1 against the top or left edge, 2 in the middle, 3 or
4 against the far edge. The two are consistent, which is the second route
for both, and the third is the measurement: driven at column 3 and row
32, the icon was in the middle cell of the window, which is what a bias
of 0 and no clamping predict.

---

## 3. The geometry, measured

Measured off a `--dump` of a real overland frame with `scripts/frames.py`
rather than derived, because §8.4's list of things that were wrong and
still ran is long enough.

**The window is 120 by 120 pixels at (8, 8)**, so x in [8, 127] and y in
[8, 127] inclusive. A cell is **24 by 24 pixels**, and cell (row *i*,
column *j*) of the window is

    x in [8 + 24j, 8 + 24j + 23],  y in [8 + 24i, 8 + 24i + 23]

That was read three ways and they agree:

* **the screen.** On a frame of the party standing on the coast, the
  terrain runs from x = 8 to x = 127 and y = 8 to y = 127 with the frame
  art beginning at 128 in both directions.
* **the redraw.** A single move changes exactly the pixels in
  `8,8,127,127` (plus the program's own status row), which is the window
  being repainted whole.
* **the composition.** The painter steps three units a cell; the blit
  takes its vertical position in units of eight scanlines and its
  horizontal in whole byte columns; and the present adds eight scanlines
  and one byte column. Three times eight is 24, and eight scanlines and
  one byte column is (8, 8).

**Every cell begins on a byte boundary and is three whole bytes wide.**
8 is byte column 1 and 24 is three bytes, so a marking confined to a cell
needs nothing shifted and nothing read back to be merged — which is the
same property that made the automap's panel a plain blit
(`machine/automap.h`).

---

## 4. The recipe that reaches the screen

No committed session reaches this screen, and every driven leg of M5-E5d
starts here. On the desktop host, under SDL's dummy drivers:

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy amberfolio <disk> START.EXE \
      --document <the code wheel PDF> --seam code-wheel \
      --fast max --until 260000000 \
      --press A@7601 --press Return@7651 \
      --press L@8951 --press J@9201 \
      --press Up@10600 --press Up@10750 --press Up@10900 --press Up@11050

* The disk is a copy of the edition's own installation **with its shipped
  save slots** — the snapshot `tests/sessions/temple.rec` pins. **Slot J
  is the one**: its party is already standing on a wilderness area, which
  is why no walk out of a city is needed and why the recipe is four
  keystrokes long.
* `--document` is not optional. Without it the code-wheel seam is inert
  by design (#115) and the run waits at the copy-protection challenge for
  ever, with every downstream symptom looking like something else
  (`docs/seams.md` §8.4).
* The party is on the overworld screen from **frame 9,552**, which is
  where the mode byte becomes 3, and the screen has settled by frame
  10,275.
* Moves are 150 frames apart, the same cadence every other driven walk in
  this tree uses. Each one repaints the window and moves the status line.
* Wandering here brings an encounter within a few virtual minutes, so a
  leg that wants a quiet walk keeps it short.

`--watch 49F3:1 --watch 49FA:1` is how the arrival is seen without
looking at a picture; filter the lines on the data segment, which is
`0CDC` on this edition.

---

## 5. The marking, decided

This is the decision with no proven design behind it, and the one a
person has to judge in the end (#257). Seven candidates were prototyped
over a real dumped frame — the stills stay on the machine, and what
follows is the decision in words and numbers.

### What was chosen

**The explored cell is redrawn one shade brighter, by setting the EGA
intensity plane over its 24 by 24 pixels.** Every pixel of it stays a
pixel the *program* drew, in the program's own sixteen-colour palette,
one step up: its dark green becomes bright green, its blue becomes bright
blue, its black becomes dark grey. Nothing is added to the screen that
the game does not already have on it.

Four reasons, in the order they decided it:

1. **It draws no shape of its own.** Every other candidate puts a mark on
   the game's screen that the game has no vocabulary for. This one has no
   mark; it has a shade. That is the strongest form of the argument
   `docs/seams.md` §3 makes for calling the program's own text drawer
   rather than rasterizing glyphs — one layer further, because there is
   no foreign artwork at all rather than none that looks foreign.
2. **It is the game's own idiom.** The program recolours this very screen
   through a palette mapping of its own (overlay 18 rel `0x06B3`, which
   recolours the overland tile sets and redraws the window). A seam that
   recolours is doing a thing this program does.
3. **It costs one plane and no read-back.** The intensity plane is plane
   3; a cell is three whole bytes by 24 scanlines; so a marked cell is
   one sequencer map-mask write away from **72 byte writes**, and a full
   window of 24 marked cells is **1,728**. The automap's panel blit is
   9,856 for comparison. Nothing is read from the video window, so no
   adapter latch is touched.
4. **The terrain stays legible.** A coastline, a road and a tile's
   outline all survive a shade change; they do not survive a hatch or a
   dither drawn over them.

**How visible is it?** Measured, because "one shade" invites the
question: across **112 overland frames and 2,800 window cells**, the cell
with the fewest pixels whose intensity bit was clear still had **198 of
its 576**. Every cell measured is visibly lifted. The failure mode it
cannot have is a cell whose art is *entirely* bright already, which none
of the 2,800 was.

**Which direction, and why not the other one.** Lowering the intensity
bit instead — a worn, darker path — looked better on grass and is
**broken on water**: the water tiles are a solid dark blue, which is
unchanged by clearing a bit that is already clear. Measured on the same
frames: a lift changes every cell, a dim changes none of the water ones.
A marking that is invisible on a fifth of the map is not a marking.

### What was rejected, and why

* **A one-pixel border inside the cell**, in white or yellow. Legible,
  and it reads as a modern overlay grid drawn on top of somebody else's
  art. Two adjacent explored cells give a doubled line with a two-pixel
  gap, which is noise; the left and right edges are mid-byte and need a
  read-modify-write each.
* **A sparse dither of a light colour across the tile.** Softer, but it
  reads as *a different terrain* rather than as a trail, and the terrain
  it is drawn over is itself a dither, so the two interfere. Pixel-
  granular, and it obscures what it covers.
* **A diagonal hatch.** The cartographer's idiom, and clearly not this
  game's; 144 read-modify-writes a cell.
* **A glyph at a corner or the centre of the cell, from the program's own
  font** (the far pointer at `0x5E20`, the same glyphs M5-E2b sets the
  automap's zone label in). Byte-aligned and cheap — an eight-by-eight
  glyph centred in a 24-pixel cell starts on a byte boundary — and it is
  the game's own lettering, which is a real argument. Rejected because
  lettering on a *map* reads as an annotation: the game writes words in
  its message rows and its menus and never on its terrain, so a letter
  here is exactly the foreign thing the font was supposed to avoid.
* **A small solid block, ring or diamond at the cell's centre.** The
  cheapest legible mark, and where the party icon itself sits, so it
  reads as a footprint. Rejected on the same ground as the border: it is
  a shape this game does not have, in a place the game draws its own
  sprite.
* **Recolouring the cell through a palette mapping of the seam's own,
  read back off the planes.** The most faithful re-expression of overlay
  18's precedent and by far the most expensive: 288 reads and 288 writes
  a cell, through the graphics controller's read-map-select, with the
  adapter's latches loaded on every read. The intensity lift is the same
  idea with the mapping fixed at `| 8`, which needs no read at all.

### The two constraints, confirmed

**The party's own cell is never marked.** Two reasons, and the second is
what makes M5-E5c's stronger claim a test rather than an argument:

* the party's icon is drawn in that cell, and a lift would recolour the
  party's own sprite — its greys and whites would go up a shade with
  everything else, which is loud and says nothing;
* with it unmarked, **arriving on a fresh overland map with nothing
  explored is pixel-identical to the run with the seam off**, and stays
  so until the party takes its first step. That is the fidelity pair
  M5-E5c asserts, and it is only true because the cell under the party is
  left alone.

The cell is still *recorded* the moment the party stands on it. It simply
appears, marked, when the party moves off it — which is what makes the
thing a trail.

**An unexplored cell is never touched.** Not one pixel, which is what
lets M5-E5d's confinement leg mask exactly the explored cells and assert
that everything else is byte for byte the seam-off run.

---

## 6. The redraw point, decided

**The return of the back-buffer present, image offset `0x649B`**, guarded
on the travel view.

The program composes the whole screen off-screen and presents the
scanlines something dirtied. So:

* at the *entry* of the present the flush has not happened, and anything
  painted there is about to be copied over;
* at its *return* the program has finished putting the screen up, and
  every path that repaints the window — the composer's own redraw, and
  each step of the icon's animation, which advances one of six phases and
  presents again — ends here. Painting there is painting last, and no
  captured frame can catch the overlay half-drawn.

**What was rejected.** Painting into the program's own back buffer after
the window painter returns, so that the program's own present carries the
marks, is memory surgery on a buffer the program reads back — its dirty
tracking, its save-under path and its next composition all read it — and
it would make the marks part of what the program believes it drew. It is
rejected unless a driven run shows the present-return path visibly
flickering, which M5-E5d will say.

**The guard, written before the action** (`docs/seams.md` §8.3 step 3).
The handler declines or does nothing at all unless every one of these
holds:

* DS is the data segment derived from `image_base()`, not whatever the
  program happened to leave loaded;
* the game mode is 3 and the view kind is 2, 3 or 4;
* **the word at `+0x1CC` of the area record is zero.** This is the guard
  a mode-and-kind test alone would miss: with it non-zero the program
  shows these same areas in the interior view, with the view kind still
  reading 2, 3 or 4, and an overlay that trusted the kind byte would
  paint its marks over the 3D view;
* no scripted move is in flight (`0x442F`);
* the area-record far pointer is inside conventional memory — §8.4's
  wild-read rule, which cost the Encamp Fix seven notices on its first
  driven run;
* the bar on the screen is the adventuring screen's own, which the
  automap's sixth point already establishes and this seam shares;
* the position has settled, on the same terms `automap_state::observe()`
  settles the interior one.

**The other two points** are the automap's, shared: the key-pending entry
`0xA6FD`, which is where the recording happens because it is where the
program is between commands, and the command-bar thunk `0x3C7A`, which is
how a seam knows whose screen it is on. This enhancement claims **no
key** — it is a setting, not a command — so it has no business at the
blocking read.

**The cost, as a number.** One repaint is at most 1,728 byte writes and
two port writes, and the repaint happens once per present that passes the
guard. A move repaints the window and presents; the icon's phase advances
one per redraw. The arrival count itself is measured in M5-E5c against
the stand-in and in M5-E5d against the program, because a number nobody
has counted is not a number.

---

## 7. The record's shape, decided

`automap_record` is a sixteen-by-sixteen map's memory: 32 bytes of
bitmap, one bit a cell, keyed (disk, area, geometry block). A wilderness
area is 16 by 36 — **576 cells, 72 bytes** — and it has no geometry block
to swap.

**One record width, widened, and a kind byte.** The sidecar goes to
version 2 and every record carries a 72-byte bitmap, of which a grid
record uses the first 32 bytes. Sixty-four records at the wider size is
about seven kilobytes, which is nothing, and a format a reader can index
into is worth more than the bytes — the same argument
`machine/automap.h` already makes for a fixed width. Carrying two record
widths in one file would mean a reader that has to walk the table to find
the *n*th record, which is the property the fixed width was chosen to
avoid.

**The kind byte is part of the key, not decoration.** An overland record
is keyed (disk, area) with its geometry block meaningless, and an
interior record on the same disk and area with a geometry block of zero
would otherwise be the same key. The kind byte is what keeps the two
apart, and it is why it is a field of the record rather than something a
reader infers.

The layout, and the reader's rule, are M5-E5b's to write into
`machine/automap.h`, which is where this tree settles what two readers
must agree about. What is decided here is the shape: **576 bits, keyed
(kind, disk, area), addressed `row * 16 + column` with row 0..35 and
column 0..15**, one width for every record, and a version-2 reader that
still opens a version-1 file because a player's existing sidecar must
open rather than be refused.

---

## 8. What this does not settle

* **The arrival count at the present's return.** §6 gives the cost of one
  repaint and not how many there are a second. M5-E5c measures it.
* **Whether the present-return path flickers.** The alternative in §6 is
  rejected on an argument; only a person watching a real walk can say
  whether the argument was right (#257).
* **Whether the lift reads as the game's own.** 2,800 cells say it is
  *visible*; nobody has yet said it is *right*, and #179's exit requires
  that somebody with a display does.
* **The other two wilderness areas.** Everything driven here was view
  kind 2 on disk 6. Kinds 3 and 4 are the same arithmetic with a
  different bias and have not been stood on.
* **A cell whose art is entirely bright.** None of the 2,800 measured
  was, and no tile set was examined directly. If one exists, its cell
  cannot be marked by this method and the mark would silently be absent
  there.
