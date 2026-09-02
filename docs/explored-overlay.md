# The explored overlay: the facts and the three decisions

*What the overworld screen is, in addresses and offsets; the pixel
geometry measured off a real frame rather than derived; the keystrokes
that put a party on that screen; and the marking, the redraw point and
the record shape decided with what each of them rejects. M5-E5a (#253) of
#179, PLAN.md §5 item 5, and step 1 of `docs/seams.md` §8.3 for the one
enhancement in the plan with no proven prior design.*

*§5 has been decided twice. The marking that shipped was an intensity
lift; the maintainer looked at it on a real run and it did not read, so
the enhancement is **fog of war** now (M5-E5f, #263). The lift is kept in
§5.1 with the six candidates it beat, because a design rejected by
looking at it is evidence.*

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
5. [The marking, decided — twice](#5-the-marking-decided--twice)
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
  is the one**, and it is the whole reason this section is short: its
  party is already standing on a wilderness area, so no walk out of a
  city is needed, no hours of play, and no save had to be constructed.
  §8 says how to build one for the two areas no shipped slot reaches.
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

## 5. The marking, decided — twice

This is the decision with no proven design behind it, and the one a
person has to judge in the end (#257). It has now been made twice,
because the person judged.

**The first design was a lift** and it shipped in M5-E5c: the game's
whole window as the game drew it, with every square the party had walked
redrawn one shade brighter. Seven candidates were prototyped over a real
dumped frame before it was chosen, and §5.1 keeps it, in full, with the
six it beat.

**The maintainer looked at it on a real run** (#263) and the answer was:

> Yeah, this does not seem to read super well. Ideally there would be a
> radius of 2 or 3 tiles that get uncovered as I traverse the terrain.
> And everything stays covered with "fog" beyond that.

That is a finding and not a preference to argue with — PLAN.md §5 item 5
makes a person with a display the exit criterion for this item precisely
because nobody had built it before. So the marking is **reversed**: what
the party has been near is drawn by the game, untouched, and everything
else in the window is covered. §5.2 is the fog, the six candidates
prototyped for *it*, and the radius. PLAN.md §5 item 5 carries the change
too, because the sentence it used to end with — "it never obscures the
unknown" — is exactly what this now does.

The stills that decided both are on the machine that made them and are
never committed (PLAN.md §6); what follows is the decision in words and
numbers.

### 5.1 The first design: an intensity lift, rejected by looking at it

**The explored cell was redrawn one shade brighter, by setting the EGA
intensity plane over its 24 by 24 pixels.** Every pixel of it stayed a
pixel the *program* drew, in the program's own sixteen-colour palette,
one step up: its dark green became bright green, its blue bright blue,
its black dark grey. Nothing was added to the screen that the game did
not already have on it.

Four reasons chose it, and they are worth keeping because three of them
survive into the fog: it **drew no shape of its own**; it was **the
game's own idiom**, since the program recolours this very screen through
a palette mapping of its own (overlay 18 rel `0x06B3`); it **cost one
plane and no read-back**, 72 byte writes a cell and 1,728 for a window;
and **the terrain stayed legible**.

It was measured, too, because "one shade" invites the question: across
112 overland frames and 2,800 window cells, the cell with the fewest
pixels whose intensity bit was clear still had **198 of its 576**. Every
cell measured was visibly lifted. *Lowering* the bit instead — a worn,
darker path — looked better on grass and was **broken on water**, which
is a solid dark blue and unchanged by clearing a bit that is already
clear.

**And it still did not read.** Being measurably different is not the same
as being legible: a shade is a difference a player has to be told about
before they can see it, and on a screen where the terrain is itself a
two-colour dither, "one step up in the palette" reads as a slightly
different patch of the same grass. The measurement was answering
"is it visible?" and the question was "does it say anything?".

**What was rejected alongside it**, each prototyped over a real frame:

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
  automap's zone label in). Byte-aligned and cheap, and it is the game's
  own lettering, which is a real argument. Rejected because lettering on
  a *map* reads as an annotation: the game writes words in its message
  rows and its menus and never on its terrain.
* **A small solid block, ring or diamond at the cell's centre.** The
  cheapest legible mark, and where the party icon itself sits, so it
  reads as a footprint. Rejected on the same ground as the border: it is
  a shape this game does not have, in a place the game draws its own
  sprite.
* **Recolouring the cell through a palette mapping of the seam's own,
  read back off the planes.** The most faithful re-expression of overlay
  18's precedent and by far the most expensive: 288 reads and 288 writes
  a cell, through the graphics controller's read-map-select, with the
  adapter's latches loaded on every read.

### 5.2 The design that shipped: fog, and it is solid black

**A cell the party has not been near is covered with black** — all four
planes cleared over its 24 by 24 pixels. Six candidates were prototyped
over one real dumped frame that has grass, coast water and the grey shore
between them in it, and four reasons decided this one.

1. **It is the one colour that cannot read as terrain.** Every candidate
   that leaves the tile's own hue showing through is, at this
   resolution, *a different kind of tile*: a half-covered green square
   reads as a duller green square. That is the same objection that
   rejected the sparse dither in §5.1, and it is worse for a fog than for
   a mark, because a fog covers most of the window rather than a square
   of it.
2. **It is the game's own vocabulary for the unknown.** Black is already
   most of this screen — the message rows under the window, the panel
   beside it, and the game's own 3D view beyond what the party can see —
   and the window sits inside the game's own drawn border. A fogged
   window therefore reads as that border framing a smaller opening,
   which is a thing this screen already looks like, rather than as a
   pattern laid over it. This is §5.1's first and strongest reason,
   surviving: there is no foreign artwork, rather than none that looks
   foreign.
3. **It is the same on every terrain.** A fog that lets the tile through
   is a different fog on grass, on water and on rough ground, and a
   player would have to learn three of them. This one covers, so there is
   one thing to learn — and it is also the answer to the failure that
   killed the *dim* direction in §5.1, since nothing here depends on what
   is underneath.
4. **It costs four planes and no read-back.** `map mask = 0x0F` and a run
   of `0x00` bytes: 72 byte writes a cell, 1,728 for a window with 24
   cells covered — the same cost the lift had, against the automap
   panel's 9,856. **Every fog that shows the terrain through needs a
   read of the video window before each write**, to load the adapter's
   latches for the pixels it is leaving alone. That is not expensive, but
   it is a seam disturbing the adapter's latches in order to draw, and
   this one does not have to.

**The five that were rejected**, all prototyped over the same frame:

* **A checkerboard of black at one pixel in two.** At 320 by 200 this is
  not a texture at all: the eye integrates it and a green tile becomes a
  flat grey-green one. It reads as terrain, and it interferes with the
  terrain's own one-pixel dither into a moiré that differs from tile to
  tile.
* **A checkerboard at two-by-two blocks.** The best of the veils: the
  period is different from the terrain's dither so there is no moiré, and
  the coastline stays faintly visible through it. Rejected on reason 1
  and reason 3 — it is still green over grass and still blue over water,
  and a fog whose colour is the colour of what it hides is a fog a player
  has to be told about.
* **A checkerboard at four-by-four blocks.** Unmistakably deliberate, and
  the loudest thing on the screen: it reads as a modern UI grid laid over
  the art, which is the border candidate's rejection in §5.1 at
  twenty-five times the size.
* **A dither at one pixel in four.** Too light to read as anything; it
  looks like a rendering fault.
* **Dropping the intensity plane** — the lift's own inverse, and the
  cheapest of all. It is invisible on water for the reason §5.1 measured,
  it flattens the grass's dither into a solid mid-green rather than
  darkening it, and what it produces reads as a *terrain type* and not as
  a covering.

### 5.3 How far the party sees, and why the radius is one

`explored_reveal_radius` in `machine/automap.h`, a **Chebyshev distance**,
default **1**: standing on a cell shows it and the eight around it, so a
walk leaves a corridor three cells wide.

**The record still holds only the cells the party stood on.** The reveal
is the *dilation* of that by the radius, computed when the window is
drawn. Two things follow, and both are the reason it is done that way:
turning the knob up changes what a player sees of the map they already
walked rather than what they have to walk again, and nothing in the
sidecar's layout (§7) moved for this change, so no sidecar anybody has is
invalidated.

**Two and three were asked for and cover nothing.** The window is five
cells across and the party is its middle cell in open country, so every
cell on the screen is already within two of the party. Measured on the
§4 walk with the constant set to 2: **523 dumped frames, and not one of
them differs from the same walk with the seam off.** At a radius of two
this enhancement is invisible except where a map's own edge pushes the
party off centre. One is the largest radius that leaves anything to
cover, and it is why the maintainer's "2 or 3" is answered with 1 and a
measurement rather than with 2.

At a radius of one, on the §4 walk, the window shows **9 uncovered cells
on arrival** and **12 from the first step onwards** — the outer ring is
fog, and so is the row the party is walking towards.

### The two constraints, and what became of them

**The party's own cell is never covered.** The party's icon is drawn
there, and a black square over it would be a black square over the
player's own sprite. At any radius of one or more the cell is revealed
anyway — the party is standing on it — but the rule is written down as
its own line in `cells_to_fog`, because it is the one mistake here that
would be a bug and not a preference.

**An unexplored cell used to be untouched, and now it is the only thing
that is touched.** That reversal costs this enhancement a fidelity claim
and `docs/seams.md` §10 says so: with the lift, arriving on a wilderness
map nobody had walked was pixel-identical to the run with the seam off,
because a lift marks what is *known* and nothing was. A fog marks what is
not, and a fresh map is nearly all of that. What is left is the honest
pair: nothing outside the window is ever touched — 523 stills of a real
run say so, at every frame — and the run with the overworld never shown
is byte for byte the run with no engine at all.

**A cell of a neighbouring area is covered.** The three wilderness areas
are three sixteen-column bands of one 44-column table and the window can
overhang, and this seam has no record for a neighbour's columns. Under
the lift, marking them would have claimed knowledge nobody had; under the
fog, *not* covering them would claim the opposite. Fog is the default
state, so an unknown cell is fogged and a cell nothing can answer for is
an unknown cell.

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
fog, is memory surgery on a buffer the program reads back — its dirty
tracking, its save-under path and its next composition all read it — and
it would make the fog part of what the program believes it drew. It is
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
  lay its fog over the 3D view;
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
  repaint and not how many there are a second. M5-E5c measures it; driven
  on the §4 walk, the seam's three points are reached **746,746** times
  over 13,075 frames.
* **Whether the present-return path flickers.** The alternative in §6 is
  rejected on an argument; only a person watching a real walk can say
  whether the argument was right (#257). `tests/visual/exp-steady.leg`
  says the screen holds still on a run, which is the file-against-file
  half of it.
* **Whether the fog reads as the game's own.** The lift's version of this
  line said 2,800 cells made it *visible* and that nobody had said it was
  *right* — and when somebody did, the answer was no (§5). This one is
  the same line about the second design, and it is the same unticked
  clause of #179: the fog has been prototyped and driven and compared
  frame against frame, and no session can say whether it reads. #263 is
  where that is asked.
* **The reveal radius.** §5.3 settles that one is the only radius that
  covers anything on this screen, and it does not settle whether one
  square of sight is the right amount of country to hand a player. That
  is the other half of the same judgement #263 asks for, and turning it
  is one constant.
* **The other two wilderness areas.** Everything driven here was view
  kind 2 on disk 6. Kinds 3 and 4 are the same arithmetic with a
  different bias and have not been stood on.

  **Reaching them does not need hours of play either**, and the way is
  worth writing down before somebody spends them. The party's whole
  overland position is four numbers, all of them in memory the save
  restores: the area id the adventuring loop reads into `0x84DC` comes
  from the area record at `+0x1E4`; the position is the two words at
  `+0x186` and `+0x188`; the word at `+0x1CC` must be zero for the travel
  view rather than the interior one; and the disk number at `0x5376` has
  to be the area's own — `0x19` on disk 6, `0x1A` on disk 7, `0x1B` on
  disk 8. So a **scratch copy** of the disk with one slot's save files
  edited to those values puts a party on any of the three, and
  `--watch 49F3:1 --watch 49FA:1` says whether it worked before anything
  else is believed. Such a save stays on the machine that made it and is
  never committed; what travels is the recipe, which is the sentence
  above.

  Nothing here needed it, because slot **J** of the edition's own shipped
  saves is already standing on view kind 2 — which is why §4 is four
  keystrokes rather than a walk.
* **A terrain the fog has not been over.** Everything prototyped and
  driven has been grass, coast water and the grey shore between them,
  which is what the area slot J starts on has near its start. Rough
  ground, forest and roads have not been under it. The fog cannot fail on
  one — it does not depend on what it covers, which is §5.2's third
  reason — but nobody has seen it there.

  The lift's version of this gap was sharper and is worth keeping as a
  contrast: **a cell whose art was entirely bright could carry no lift at
  all**, and nothing would have said so. None of the 2,800 cells measured
  was, and no tile set was examined directly. A marking that depends on
  what is underneath has failure modes a marking that covers does not.
