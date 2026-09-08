# The explored overlay: the facts and the three decisions

The overworld screen in addresses and offsets, the pixel geometry
measured off a real frame, the keystrokes that put a party on that
screen, and the marking, the redraw point and the record shape as
decided (#179, PLAN.md §5 item 5). The seam is `seam_explored.cpp`; the
record lives in `machine/automap.h`.

Everything below is an address, an offset, a length, arithmetic or a
measurement. No game code, data, text or byte sequence appears here.

## 1. What the overworld screen is

The program's **wilderness travel view**, not the sixteen-by-sixteen grid
the automap panel maps (#173). An area is **16 columns by 36 rows** (576
cells); the screen shows a **5x5 window** of overhead tiles that scrolls
with the party, with the party's icon in one cell. The program is on that
screen when the game-mode byte is 3 and the view kind is 2, 3 or 4, one
value per area.

- The automap seam is gated on mode 4 and view kind 1 and never runs
  here; this seam reuses its store and host callout, and adds the record.
- The party's overland position is two words in the **area record**, not
  the data-segment bytes every other screen uses (§2).
- The screen is composed off-screen into a back buffer and **presented**
  by a resident routine that flushes only dirtied scanlines. The seam
  draws at the *return* of that present (§6).

## 2. The fact table, each fact twice

Every row carries the route that found it and an independent route that
agreed (`docs/seams.md` §8.1). "Driven" means a run under the §4 recipe.

### The screen, in the data segment

| what | where | second route |
| --- | --- | --- |
| game mode; **3** is the travel view | `0x49F3` | used by `seam_cheats.cpp`, `seam_encamp_fix.cpp`, `seam_automap.cpp`; driven, 3 on arrival |
| view kind; **2, 3, 4** are the three wilderness areas | `0x49FA` | the redraw's own test is `> 1 && < 5`; driven, 2 on the area in §4 |
| a scripted move is in flight | `0x442F` | `seam_automap.cpp` reads the same byte |
| the disk the area's files come from | `0x5376` | driven, 6 on the area in §4 |
| the area id | `0x84DC` | driven, `0x19`; the automap's zone table has that id |
| the party's facing, 0 N / 2 E / 4 S / 6 W | `0x6AAF` | `docs/playable.md`'s watch list; driven, 4 with the status line reading `S` |
| the column-bias table, indexed by view kind | `0x3C76` | begins where the terrain table ends (`0x3648 + 0x2C * 36 = 0x3C78`); its live entries 0, 13, 26 are what the status line adds to the column for kinds 3 and 4 |
| the overland terrain table: 36 rows, stride `0x2C`, three 16-column bands at those biases | `0x3648` | the move step and the window painter index it with the same arithmetic |

The seam reads the bias out of the program at `0x3C76`; the three values
are the check, not the implementation.

### The area record, through the far pointer at `0x49D2`

`0x49D2` is a **far pointer** (offset, then segment), already read through
by `seam_encamp_fix.cpp` and `seam_automap.cpp`.

| what | offset in the record | second route |
| --- | --- | --- |
| the party's overland **column**, a word, clamped 0..15 | `+0x186` | the move step and the status line both read it; the status line prints it |
| the party's overland **row**, a word, clamped 0..35 | `+0x188` | same |
| non-zero: the program draws these areas in **3D** whatever the view kind says | `+0x1CC` | the redraw consults it first and forces the interior path (§6) |

**`0x6AAD`/`0x6AAE` are not the overland position.** They are the
position everywhere else; on the overland they hold the interior grid's
cell (driven: `0x0B`, `0x0D` with the status line reading `3, 32`).

### The routines

Image offsets: a decompiled `seg:off` with load segment `0x1000` maps to
`(seg - 0x1000) * 16 + off`.

| routine | where | what it is |
| --- | --- | --- |
| the back-buffer **present** | image `0x6192` | flushes dirtied scanlines: composes into the second display page a plane at a time, then latch-copies the span to the visible page; opens `push bp / mov bp, sp` and reads the adapter byte, the back-buffer far pointer, the per-row dirty flags and the per-row min/max/destination x |
| its **return** | image `0x649B` | `mov sp, bp / pop bp / retf`, the only far return in the routine |
| the per-mode **composer** | overlay 26 rel `0x016D` | picks the interior or travel path; on the travel path computes the window's top-left and calls the two painters |
| the 5x5 **window painter** | overlay 27 rel `0x0000`, `retf 4`, args row then column | 25 cells, cell index stepping by one and blit position by three |
| the per-cell **blit thunk** | image `0x3EBA` into overlay 32 rel `0x00F1`, `retf 8` | one overhead tile into the back buffer |
| the party **icon painter** | overlay 26 rel `0x0000`, `retf 0` | draws the icon in the party's cell and presents |

Overlay rows, from the overlay file's own table: 26 at file offset
`0x02DB87` length `0x2A5`; 27 at `0x02DE38` length `0x288`; 32 at
`0x031A6C` length `0x645`. No point this seam needs is in an overlay.

### The window's arithmetic

    col = bias(view kind) + column - 2, clamped to [0, 0x27]
    row = row - 2,                      clamped to [0, 0x1F]

The icon painter separately computes which of the five bands the party is
in (0 or 1 against the near edge, 2 in the middle, 3 or 4 against the
far edge); the two agree. Driven at column 3, row 32, the icon was in the
middle cell.

## 3. The geometry, measured

Measured off a `--dump` of a real overland frame with `scripts/frames.py`.

**The window is 120x120 pixels at (8, 8)**: x and y in [8, 127]
inclusive. A cell is **24x24**, and cell (row *i*, column *j*) is

    x in [8 + 24j, 8 + 24j + 23],  y in [8 + 24i, 8 + 24i + 23]

Three readings agree: the frame art begins at 128 in both directions; a
single move changes exactly `8,8,127,127` plus the status row; and the
painter steps three units a cell, the blit takes its vertical position
in units of eight scanlines and its horizontal in byte columns, and the
present adds eight scanlines and one byte column.

**Every cell begins on a byte boundary and is three whole bytes wide**,
so a marking confined to a cell needs nothing shifted.

## 4. The recipe that reaches the screen

`tests/sessions/wild.rec` and `wild-trail.rec` are this screen as a
pair; by hand, on the desktop host under
SDL's dummy drivers:

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy amberfolio <disk> START.EXE \
      --code-wheel-answered \
      --fast max --until 232156800 \
      --press L@7551 --press J@7801 \
      --press Up@9200 --press Up@9350 --press Up@9500 --press Up@9650

- The disk is the edition's own installation **with its shipped save
  slots** (the snapshot `tests/sessions/temple.rec` pins). **Slot J**'s
  party is already standing on a wilderness area (view kind 2, disk 6).
- The party is on the overworld from **frame 8,152** (mode byte becomes
  3); the screen has settled by 8,875. Moves are 150 frames apart.
  Wandering brings an encounter within a few virtual minutes.
- `--watch 49F3:1 --watch 49FA:1` shows the arrival without a picture;
  the data segment is `0CDC` on this edition.

**Reaching the other two areas** needs an edited save; the edit stays on
the machine that made it. One byte in the slot's `savgam?.dat` carries
the view kind:

| file offset | what |
| --- | --- |
| `0x3206` | the **view kind**: 1 interior, 2/3/4 the three wilderness areas |
| `0xE25` | the disk the area's files load from (the one that sticks in `0x5376`) |
| `0x0` | the disk the save was made on (overwritten during the load) |
| `0x1` | where the area record's image begins: record `+0x186`, `+0x188`, `+0x1CC`, `+0x1E4` are file `0x187`, `0x189`, `0x1CD`, `0x1E5` |

Kind 3: `0x3206` = 3, `0xE25` = `0x0` = 7, `0x1E5` = `0x1A`. Kind 4: 4,
8, `0x1B`. Area ids are the zone table's (`seam_automap.cpp`): 25 on
disk 6, 26 on disk 7, 27 on disk 8. `--watch 49F3:1 --watch 49FA:1
--watch 5376:1 --watch 84DC:1` confirms the arrival. The position words
are the party's local column and row on its band; slot J's party stands
on water on kinds 3 and 4, and a party on water does not walk. `0x5376`
is a data-segment global, not a save field, and `+0x1E4` alone changes
only what the automap records under.

## 5. The marking, decided

**Fog of war.** A cell the party has not stood on is hazed with a
**one-pixel checkerboard of palette index 0** on half its 24x24 pixels;
the other half are the program's own pixels, untouched. The parity is the
**screen's** (`x + y` even in screen coordinates), so the pattern runs
unbroken across cell boundaries
(`TheCheckerRunsUnbrokenAcrossTheSquareBoundaries`). The colour comes
out of the graphics controller's set/reset register so one write paints
all four planes, and because the covering keeps half the pixels it must
load the adapter's latches with a read before each write: 72 reads and
72 writes a cell, 1,728 each for a window with 24 cells covered
(`fog_cell` in `seam_explored.cpp`; `docs/seams.md` §3's masked-write
rule).

The marking was chosen by the maintainer looking at it, three times
(#263, #299), which is this item's exit rule. Rejected candidates, each
prototyped over a real frame:

| candidate | rejected because |
| --- | --- |
| **intensity lift** on walked cells (shipped first; §5.1) | measurably visible on 2,800 cells and still did not read: a shade is a difference a player has to be told about |
| lowering the intensity bit | invisible on water, which is solid dark blue |
| one-pixel border inside the cell | reads as a modern overlay grid; doubled lines between neighbours; mid-byte edges need read-modify-write |
| sparse light dither across the tile | reads as a different terrain; interferes with the tile's own dither |
| diagonal hatch | cartographer's idiom, not this game's; 144 read-modify-writes a cell |
| a glyph from the program's font (`0x5E20`) | lettering on a map reads as annotation; the game never writes on terrain |
| a block, ring or diamond at the cell centre | a shape the game does not draw, where the party sprite sits |
| recolouring through a seam-owned palette map | 288 reads and 288 writes a cell |
| **solid black** fog | throws away the shape of the country the party is standing at the edge of |
| **dark-grey (index 8) checker** (shipped between #263 and #299) | reads thin in play; over mountain rock a third of the covering writes grey onto grey |
| light-grey checker | reads as paler terrain |
| two-by-two dark-grey checker | reads as a pattern rather than a haze |
| dither at one pixel in four | too light; looks like a rendering fault |
| dropping the intensity plane | invisible on water; flattens grass into a terrain type |

### 5.1 The first design: an intensity lift

Every walked cell redrawn one shade brighter by setting the intensity
plane over its 24x24 pixels; 72 byte writes a cell, no read-back, the
game's own idiom (overlay 18 rel `0x06B3` recolours this screen through a
palette map). Rejected by looking at it (#263).

### 5.2 The design that shipped: fog, a black checker

As above. It was dark grey (index 8) at a radius of one from #263 until
#299; a walk showed the grey thin and black plainly a covering beside
the party's own clear square. Black cannot read as terrain, is the game's
own colour for what is not there, and is the same on every terrain.

### 5.3 How far the party sees

`explored_reveal_radius` in `machine/automap.h`, a **Chebyshev
distance**, **0**. The record holds only cells the party stood on; the
reveal is the dilation by the radius at draw time, so the knob can turn
without invalidating a sidecar. Radius 2 or 3 covers nothing: the window
is five across with the party in the middle (523 driven frames at 2 were
byte-identical to seam-off). Radius 1 uncovers a corridor three cells
wide, which fills the map faster than the party explores it (#299).

Constraints: **the party's own cell is never covered** (its own line in
`cells_to_fog`, and the only certainly clear cell at radius 0); a cell of
a neighbouring area's band is covered, because fog is the default and
the seam has no record for it; nothing outside `8,8,127,127` is ever
touched. The reversal from lift to fog cost the claim "arriving on a
fresh map is pixel-identical to off"; `docs/seams.md` §10 says so.

## 6. The redraw point, decided

**The return of the back-buffer present, image `0x649B`**, guarded on
the travel view. At the entry the flush has not happened; at the return
every path that repaints the window (the composer's redraw and each of
the icon's six animation phases) has finished, so painting there is
painting last and no captured frame catches the overlay half-drawn.

Rejected: painting into the program's own back buffer after the window
painter returns. The program reads that buffer back (dirty tracking,
save-under, next composition), so the fog would become part of what the
program believes it drew.

**The guard.** The handler does nothing unless all of these hold:

- DS is the data segment derived from `image_base()`;
- game mode is 3 and view kind is 2, 3 or 4;
- **the word at `+0x1CC` of the area record is zero** (otherwise the
  program shows these areas in 3D with the kind byte unchanged);
- no scripted move is in flight (`0x442F`);
- the area-record far pointer is inside conventional memory
  (`docs/seams.md` §8.4's wild-read rule);
- the bar on the screen is the adventuring screen's own (the automap's
  sixth point, shared);
- the position has settled, on `automap_state::observe()`'s terms.

The other two points are the automap's, shared: the key-pending entry
`0xA6FD`, where the recording happens, and the command-bar thunk
`0x3C7A`. This seam claims **no key**; it is a setting. It also paints at
the keyboard poll, because a trail read in beside a save under a
standing party would otherwise never show (`docs/seams.md` §8.4).

One repaint is at most 1,728 byte writes and two port writes; driven on
the §4 walk the seam's three points are reached 746,746 times over
13,075 frames.

## 7. The record's shape, decided

`automap_record` is keyed (kind, disk, area, geometry block). Sidecar
**version 2** widens every record to a **72-byte bitmap** (576 bits; a
grid record uses the first 32 bytes), one width for every record so a
reader can index into the file. An overland record is addressed
`row * 16 + column`, row 0..35, column 0..15, with its geometry block
meaningless; the **kind byte is part of the key**, since an interior
record with geometry block zero would otherwise collide. A version-2
reader still opens a version-1 file. The layout is in
`machine/automap.h`.

## 8. What this does not settle

- Whether the present-return path flickers in play; `tests/visual/exp-steady.leg`
  is the file-against-file half.
- Whether black at radius zero reads in play: that setting was asked for
  from a walk and has not itself been walked.
- Forest and roads have not been under the fog. Mountain rock has, on
  kinds 3 and 4, and it reads; a tile drawn entirely in index 0 would
  carry nothing, and no tile set has been read for one.
- The other two areas have been arrived on and walked (#267): the bias
  needed no correction, and every dumped frame keeps the difference from
  seam-off inside `8,8,127,127`.
