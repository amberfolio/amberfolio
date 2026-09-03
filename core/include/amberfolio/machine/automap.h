// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap's exploration state: what the party has seen, per map, and
// what the panel is currently doing about it. M5-E2 (#173), PLAN.md §5
// item 3.
//
// This is the automap seam's memory (`seam_automap.cpp`). It is here, in
// core and beside the machine, for the reason `overlay.h` is: it is
// **observation**, derived from the machine by something that watches it,
// and it is not machine state.
//
//
// Not machine state, and the three sentences that say so
// ------------------------------------------------------
//
// The same three the seam engine's own configuration gets (`seam.h`,
// "The fidelity boundary, as a test"), because this is the same kind of
// thing:
//
//   * `machine::reset()` drops it. A reset machine has no program, so it
//     has explored nothing.
//   * The state serialization (`state.h`) never sees it. A machine with
//     an explored map hashes as the machine without one, which is what
//     lets the fidelity pair below be a test rather than an argument.
//   * A replay **reconstructs** it, exactly as it reconstructs the
//     overlay tracker: the same program, the same keys and the same seam
//     set walk the same squares and reveal the same cells. Nothing about
//     it has to be recorded, because nothing about it is an input.
//
// What is *not* like the overlay tracker is where it comes from. The
// tracker is filled by the DOS layer as the program reads; this is filled
// by a seam, which is the only thing allowed to be looking (PLAN.md §5).
// With the automap seam off, every field below stays at its power-on
// value for the whole of a run.
//
//
// Two halves, and only one of them outlives a session
// ---------------------------------------------------
//
// **What has been seen** — `automap_record`, one per map identity — is
// the part a player would be sorry to lose, and it is the part a host
// persists beside the save through the VFS door (#170). Its layout is
// therefore versioned and decided here rather than in a renderer, because
// the explored overlay (#179) reads the same records for the overworld
// map and must find the same shape.
//
// **What the panel is doing** — open or not, covered or not, where the
// party was when it last settled — is a session's business and no more.
// It is here because the seam has nowhere else to put it: a handler is a
// plain function pointer with `seam_context::scratch_words` words of its
// own, which is sixteen bytes, and one map's fog alone is thirty-two.
//
//
// The map identity is three bytes, and the third one is the trap
// --------------------------------------------------------------
//
// A map is not (disk, area). Several of the program's areas swap the
// whole sixteen-by-sixteen grid underneath a fixed area id — a quadrant
// of a castle's grounds, the caverns under a well — through the program's
// own view-config path rather than through an area change. Keying only on
// (disk, area) hands a freshly swapped grid the previous one's explored
// cells, which paints a map the party has never walked. The third byte is
// the loaded geometry block, which does change, and it is what makes the
// key honest.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {

/// The grid every one of the program's interior maps is: sixteen by
/// sixteen cells, addressed `y * 16 + x`, coordinates wrapping at four
/// bits.
inline constexpr unsigned automap_map_side = 16;
inline constexpr unsigned automap_map_cells =
    automap_map_side * automap_map_side;

/// The overland map a wilderness area is (M5-E5b, #254): the same sixteen
/// columns and **thirty-six** rows, addressed `row * 16 + column` with
/// neither coordinate wrapping. The program clamps the party's column to
/// 0..15 and its row to 0..35 in its own move step, which is where both
/// numbers come from; `docs/explored-overlay.md` §2 has the offsets they
/// live at and the second route for each.
inline constexpr unsigned automap_overland_columns = 16;
inline constexpr unsigned automap_overland_rows = 36;
inline constexpr unsigned automap_overland_cells =
    automap_overland_columns * automap_overland_rows;

/// Which kind of map a record is about, and therefore how its bitmap is
/// addressed and what its identity means. Values are part of the
/// persisted layout, so they are spelled rather than left to the
/// enumeration's order.
enum class automap_map_kind : std::uint8_t {
  /// One of the program's interior grids: sixteen by sixteen, identified
  /// by (disk, area, geometry block).
  grid = 0,
  /// One wilderness area's overland map: sixteen by thirty-six,
  /// identified by (disk, area) — there is no geometry block to swap out
  /// there, so the third byte is zero and means nothing.
  overland = 1,
};

/// One bit per cell, at the width of the **widest** kind, because a
/// record is one fixed width whatever it is about.
///
/// A grid uses the first `automap_grid_seen_bytes` of it and leaves the
/// rest zero. The alternative — two record widths in one table — buys
/// forty bytes a record and costs the property the fixed width was chosen
/// for: that a reader can index into the table rather than walk it. Sixty-
/// four records at this width is about seven kilobytes.
inline constexpr std::size_t automap_grid_seen_bytes = automap_map_cells / 8;
inline constexpr std::size_t automap_overland_seen_bytes =
    automap_overland_cells / 8;
inline constexpr std::size_t automap_seen_bytes = automap_overland_seen_bytes;

/// The bit index of a cell on a map of this kind, or `automap_no_cell`
/// when the coordinates are not on such a map.
///
/// A grid's coordinates **wrap at four bits**, which is what the
/// program's own map addressing does and is therefore not an error there.
/// An overland map's do not: a row of 36 is off the map, and revealing
/// some wrapped-around cell instead would paint a square nobody has
/// walked, which is the failure `automap.h` refuses everywhere else.
inline constexpr std::size_t automap_no_cell = static_cast<std::size_t>(-1);
[[nodiscard]] std::size_t automap_cell_index(automap_map_kind kind, unsigned x,
                                             unsigned y) noexcept;

/// Where the panel goes, in framebuffer pixels, and why it goes there.
///
/// The program draws the adventuring screen's right-hand frame with its
/// interior at text rows 1..15 and columns 0x11..0x26, and puts the
/// frame's border tiles one cell *outside* that — so every pixel of the
/// interior is the panel's by construction and no part of the frame art
/// can reach into it. Of those fifteen rows the last, row 0x0f, is the
/// program's own status line (the coordinates, the compass and the
/// clock), and it stays the program's: it is redrawn as the clock ticks,
/// and a panel that claimed it would flicker once a minute for nothing.
///
/// That leaves rows 1..14 by columns 0x11..0x26 — x in [136, 312) and y
/// in [8, 120), which is 176 by 112 pixels. Both numbers matter:
///
///   * 136 is a byte boundary (column 0x11 times eight) and 176 is
///     twenty-two whole bytes, so a row of the panel is twenty-two whole
///     bytes of each plane. Nothing is shifted and nothing is read back
///     to be merged.
///   * 112 is sixteen sevens exactly, so the map fills the panel's whole
///     height at seven-pixel cells with no slack, and the remaining
///     176 - 112 = 64 pixels are the eight text columns the zone label
///     is set in.
inline constexpr unsigned automap_panel_x = 136;
inline constexpr unsigned automap_panel_y = 8;
inline constexpr unsigned automap_panel_width = 176;
inline constexpr unsigned automap_panel_height = 112;

/// The map's cell size, and where the map ends and the label band begins.
/// How many pixels the panel is, all told — the size of the buffer it is
/// rendered into, in the type an array wants.
inline constexpr std::size_t automap_panel_pixels =
    std::size_t{automap_panel_width} * automap_panel_height;

inline constexpr unsigned automap_cell_pixels = 7;
inline constexpr unsigned automap_band_x =
    automap_map_side * automap_cell_pixels;

/// How many characters of the game's own eight-pixel font fit across that
/// band, which is what the zone label is wrapped to (M5-E2b). Eight is
/// what the map's size costs: at ten columns the cells could never exceed
/// (176 - 80) / 16 = 6 pixels, so the wider band and the bigger map are
/// exclusive and this is the side of that trade the panel is on.
inline constexpr unsigned automap_text_columns =
    (automap_panel_width - automap_band_x) / 8;

/// The panel rect again, in the program's own text cells — which is the
/// unit its region clears are expressed in, and therefore the unit the
/// "has something else taken these cells" test has to be made in.
inline constexpr std::uint8_t automap_panel_top_row = 0x01;
inline constexpr std::uint8_t automap_panel_bottom_row = 0x0E;
inline constexpr std::uint8_t automap_panel_left_col = 0x11;
inline constexpr std::uint8_t automap_panel_right_col = 0x26;

// ---------------------------------------------------------------------------
// The overworld screen's geometry (the explored overlay, #179)
// ---------------------------------------------------------------------------
//
// The other screen these records are drawn on, and the other seam that
// reads them (`seam_explored.cpp`). It is here for the reason the panel's
// rect above is: it is arithmetic about the program's screen that a test
// has to be able to check without a machine, and it belongs beside the
// records it addresses.
//
// **Measured, not derived** (`docs/explored-overlay.md` §3). The program
// composes a five-by-five window of overhead tiles into its back buffer
// and presents it; on the screen that window is 120 by 120 pixels at
// (8, 8), so a cell is 24 by 24. Three routes agreed: the pixels on a
// real dumped frame, the pixels one move repaints, and the composition
// arithmetic — three units a cell, a unit one byte column across and
// eight scanlines down, with the present adding one of each.
//
// **Every cell begins on a byte boundary and is three whole bytes wide**,
// which is what lets a marking be written into the planes without
// shifting anything and without reading anything back.

inline constexpr unsigned explored_window_cells = 5;
inline constexpr unsigned explored_window_x = 8;
inline constexpr unsigned explored_window_y = 8;
inline constexpr unsigned explored_cell_pixels = 24;

/// **How far a party sees**, in cells, as a Chebyshev distance: standing
/// on a cell reveals it and everything within this many cells of it, and
/// the rest of the window is fogged (M5-E5f, #263). It is the one number
/// in this enhancement a person is meant to turn, so it is named here
/// rather than buried in the seam, and the record stores only the cells
/// the party actually *stood* on — the reveal is the dilation of that,
/// computed when the window is drawn, so turning this knob does not
/// invalidate anybody's stored map.
///
/// **Why one and not two**, which is the question the number invites.
/// The program's window is five by five and the party is its middle cell
/// in open country, so every cell on the screen is already within two of
/// the party: at a radius of two nothing on that screen is ever fogged
/// except where a map's own edge pushes the party off centre, and the
/// enhancement would be invisible for the whole of a walk across the
/// middle of an area. One is the largest radius that leaves anything to
/// cover. Two and three were asked for and were measured to fog nothing
/// (`docs/explored-overlay.md` §5).
inline constexpr int explored_reveal_radius = 1;

/// The top-left cell of the window, in the program's own terms: a column
/// of the 44-column overland terrain table and a row of the map.
struct explored_window {
  int col{};
  int row{};
};

/// Where the window's top-left cell is, exactly as the program's own
/// per-mode composer computes it: `bias + column - 2` clamped to
/// [0, 0x27] across, and `row - 2` clamped to [0, 0x1F] down. The bias is
/// the byte the program keeps per view kind, which puts each of the three
/// wilderness areas in its own sixteen-column band of the one table; a
/// seam reads it out of the program rather than carrying it.
///
/// A pure function, with the clamps that decide where the party's own
/// icon sits in the window — the middle cell in open country, and the
/// second or the fourth against an edge.
[[nodiscard]] explored_window explored_window_top_left(int bias, int column,
                                                       int row) noexcept;

/// What a marked cell is. Values are part of the persisted layout, so
/// they are spelled rather than left to the enumeration's order.
enum class automap_marker : std::uint8_t {
  /// Nothing marked here.
  none = 0,
  /// Where the party arrived on this map.
  entrance = 1,
  /// The cell the party was standing on when the map changed.
  exit = 2,
};

/// How many marks one map may carry. A bound rather than a growth
/// policy, which is the house pattern for anything the machine feeds
/// (`seam_engine::max_seams`, the trace ring): a map with more than a
/// dozen ways in and out is not one this can usefully draw anyway.
inline constexpr std::size_t automap_max_markers = 12;

/// The sidecar's layout version, and the four bytes that name one.
///
/// **The layout is decided here** rather than by whichever host happens
/// to write it, because two things read these records: the panel (#173)
/// and the explored overlay (#179), and a host is not the place to settle
/// what they must agree about. A reader that does not know a version
/// refuses the file and says so — an exploration table read as the wrong
/// shape paints a map nobody has walked, which is worse than an empty
/// one.
///
/// **Version 2 is the overland's** (M5-E5b, #254): a kind byte, and a
/// bitmap wide enough for 576 cells. A reader here reads 1 *and* 2 and
/// writes 2, because a player's existing `AFMAP.DAT` has to open rather
/// than be refused — a version bump that threw the map away would be the
/// same loss the sidecar exists to prevent.
inline constexpr std::uint8_t automap_sidecar_version = 2;
inline constexpr std::uint8_t automap_sidecar_first_version = 1;
inline constexpr std::array<char, 3> automap_sidecar_magic{'A', 'F', 'M'};

/// One record on disk, fixed width, little-endian where it is wider than
/// a byte. Fixed rather than packed: a table of at most sixty-four maps
/// is four and a half kilobytes at its very largest, and a format a
/// reader can index into is worth more than the bytes.
///
///     0   1   the disk
///     1   1   the area
///     2   1   the geometry block (zero on an overland record)
///     3   1   the map kind (`automap_map_kind`)
///     4   1   how many marks are live
///     5   72  one bit per cell, `y * 16 + x`, low bit of a byte first
///     77  12  the marks' x
///     89  12  the marks' y
///     101 12  the marks' kind
///
/// **The kind byte is part of the key, not decoration.** An overland
/// record is keyed (disk, area) with its geometry block zero, and an
/// interior record on the same disk and area whose geometry block happens
/// to be zero would otherwise be the same record.
inline constexpr std::size_t automap_sidecar_record_bytes =
    5 + automap_seen_bytes + (3 * automap_max_markers);

/// And version 1's, which this build still reads: no kind byte, and a
/// bitmap of 32. Every record in such a file is a grid.
///
///     0   1   the disk
///     1   1   the area
///     2   1   the geometry block
///     3   1   how many marks are live
///     4   32  one bit per cell
///     36  12  the marks' x
///     48  12  the marks' y
///     60  12  the marks' kind
inline constexpr std::size_t automap_sidecar_v1_record_bytes =
    4 + automap_grid_seen_bytes + (3 * automap_max_markers);

/// And the header in front of them:
///
///     0   3   "AFM"
///     3   1   the version
///     4   2   how many records follow
///     6   2   how many bytes each of them is
inline constexpr std::size_t automap_sidecar_header_bytes = 8;

/// One map's exploration.
struct automap_record {
  /// Whether this slot holds a map at all.
  bool used{false};

  /// Which kind of map this is, and therefore how `seen` is addressed and
  /// whether `geo` means anything.
  automap_map_kind kind{automap_map_kind::grid};

  /// The map's identity, three bytes (see the header comment): the disk
  /// the area's files come from, the area id, and the loaded geometry
  /// block that distinguishes an in-place grid swap from the area it
  /// swapped inside. On an overland record the third is zero and is not
  /// part of the identity.
  std::uint8_t disk{};
  std::uint8_t area{};
  std::uint8_t geo{};

  /// One bit per cell, `y * 16 + x`, low bit of a byte first. A grid
  /// leaves everything past `automap_grid_seen_bytes` zero.
  std::array<std::uint8_t, automap_seen_bytes> seen{};

  /// The marks, and how many of them are live.
  std::uint8_t marker_count{};
  std::array<std::uint8_t, automap_max_markers> marker_x{};
  std::array<std::uint8_t, automap_max_markers> marker_y{};
  std::array<automap_marker, automap_max_markers> marker_kind{};
};

/// Why the panel drew a door leaf on a face (#268).
///
/// Four rules answer "is this face a door", and they draw the same two
/// yellow pixels. Which of them drew a given leaf is not recoverable from
/// the picture, and until #268 it was not recoverable from anything: the
/// only maps ever driven were New Phlan's, where the last of the four is
/// the only one that ever ran. So the panel counts them as it draws, and
/// a host prints the tally — which is what makes a screenshot of a leaf
/// evidence for a rule rather than an inference about one.
enum class automap_door_evidence : std::uint8_t {
  /// The face itself is shut. Unarguably a door, and it needs no rule.
  shut,
  /// A way through, whose *kind* was seen shut somewhere on this map.
  /// This is the rule M5-E2a is built on.
  seen_kind,
  /// A way through, whose kind only the seam's table of every shut face
  /// in the shipped data names. The fallback.
  table_kind,
  /// A way through on a map where nothing at all is known about the wall
  /// sets, so the pre-nibble rule stands: a passable face is a door.
  no_evidence,
};

/// How many leaves the last drawn panel drew, by evidence.
///
/// Presentation state, in the sense the drawn signature is: reset at the
/// top of every draw, never serialized, and nothing in the machine reads
/// it. A counter, not a decision — the panel's pixels are exactly what
/// they would be without it.
struct automap_door_tally {
  std::uint16_t shut{};
  std::uint16_t seen_kind{};
  std::uint16_t table_kind{};
  std::uint16_t no_evidence{};

  friend constexpr bool operator==(const automap_door_tally&,
                                   const automap_door_tally&) = default;
};

/// Everything the automap knows, for one machine.
class automap_state {
 public:
  /// How many maps are remembered at once.
  ///
  /// The program ships thirty-five areas and a handful of in-place grid
  /// swaps inside them, so this is roughly twice what one playthrough can
  /// reach. A full table reuses the oldest-claimed slot rather than
  /// refusing to record — losing the first map's fog is a worse answer
  /// than losing the current one's, but both are better than a seam that
  /// stops working, and neither is reachable by the program this build
  /// targets.
  static constexpr std::size_t max_records = 64;

  /// Drop everything: no map explored, no panel, nothing settled. What
  /// `machine::reset()` calls, and what a test calls to start again.
  void clear() noexcept;

  // --- what has been seen ---------------------------------------------

  /// The record for this map identity, claiming a slot if there is not
  /// one yet. Never null.
  [[nodiscard]] automap_record& record_for(automap_map_kind kind,
                                           std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) noexcept;

  /// The interior grid's form of it, which is what every caller before
  /// M5-E5b meant.
  [[nodiscard]] automap_record& record_for(std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) noexcept {
    return record_for(automap_map_kind::grid, disk, area, geo);
  }

  /// The overland's form: keyed (disk, area), with no geometry block.
  [[nodiscard]] automap_record& record_for_overland(
      std::uint8_t disk, std::uint8_t area) noexcept {
    return record_for(automap_map_kind::overland, disk, area, 0);
  }

  /// The record for this map identity, or null if none has been claimed.
  /// The const half, for a host that is persisting and a test that is
  /// asking.
  [[nodiscard]] const automap_record* find(automap_map_kind kind,
                                           std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) const noexcept;

  [[nodiscard]] const automap_record* find(std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) const noexcept {
    return find(automap_map_kind::grid, disk, area, geo);
  }

  [[nodiscard]] const automap_record* find_overland(
      std::uint8_t disk, std::uint8_t area) const noexcept {
    return find(automap_map_kind::overland, disk, area, 0);
  }

  /// Whether a cell has been seen. On a grid the coordinates wrap at four
  /// bits, the way the program's own do; on an overland map a cell off
  /// the map is answered `false` rather than wrapped.
  [[nodiscard]] static bool seen(const automap_record& map, unsigned x,
                                 unsigned y) noexcept;

  /// Mark a cell seen. True if it had not been — and false, touching
  /// nothing, for a cell that is not on this kind of map.
  bool reveal(automap_record& map, unsigned x, unsigned y) noexcept;

  /// Put a mark on a cell. Deduplicated by position, first kind winning,
  /// so a cell that is both the way in and the way out keeps the way in.
  /// Silently does nothing once a map's marks are full.
  void mark(automap_record& map, unsigned x, unsigned y,
            automap_marker kind) noexcept;

  /// What is marked at a cell, or `none`.
  [[nodiscard]] static automap_marker marker_at(const automap_record& map,
                                                unsigned x,
                                                unsigned y) noexcept;

  /// How many records are in use — for a host that is about to write them
  /// out, and for a test.
  [[nodiscard]] std::size_t records_used() const noexcept;

  /// The records themselves, for the same two readers.
  [[nodiscard]] const std::array<automap_record, max_records>& records()
      const noexcept {
    return records_;
  }

  /// Bumped every time anything above changes. A host persists when this
  /// has moved since it last wrote; the renderer redraws for the same
  /// reason. Never reset except by `clear()`, so "has it changed" is one
  /// comparison and cannot be fooled by a change that happens to restore
  /// an earlier value.
  [[nodiscard]] std::uint32_t serial() const noexcept { return serial_; }

  // --- what the panel is doing ----------------------------------------

  /// Whether the player has asked for the panel. **False at power-on**,
  /// which is the whole of this seam's fidelity claim: a seam that is on
  /// and never asked has drawn nothing, so the run is the run it would
  /// have been with the seam off.
  [[nodiscard]] bool panel_open() const noexcept { return panel_open_; }
  void set_panel_open(bool open) noexcept { panel_open_ = open; }

  /// Whether the panel's pixels are currently on the planes because this
  /// seam put them there and nothing has painted over them since.
  [[nodiscard]] bool panel_on_screen() const noexcept {
    return panel_on_screen_;
  }
  void set_panel_on_screen(bool up) noexcept { panel_on_screen_ = up; }

  /// Whether something other than the party roster owns the panel's
  /// cells. The program tells the seam so through its own drawing calls;
  /// the mode byte cannot, because a character sheet, an item list and a
  /// message all take the whole screen with the mode still "adventuring".
  [[nodiscard]] bool panel_covered() const noexcept { return panel_covered_; }
  void set_panel_covered(bool covered) noexcept;

  /// Whether the bar the program last put up is the adventuring screen's
  /// own (M5-E2d). **False at power-on**, and false again the moment
  /// anything else asks the player something — a vendor's yes/no, a
  /// script's menu, a shop, the camp screen.
  ///
  /// It is here rather than read out of the machine each time because the
  /// machine stops holding it: the program hands its menu to its one
  /// input routine as an argument, and the argument is gone the moment
  /// that routine returns. This is the seam noting what it saw go past,
  /// which is what `seam.h`'s "keep in it only what the machine has
  /// stopped holding" is for — and it is presentation state exactly as
  /// the three above are, dropped by `clear()` and never serialized.
  [[nodiscard]] bool at_command_bar() const noexcept { return at_command_bar_; }
  void set_at_command_bar(bool at) noexcept { at_command_bar_ = at; }

  /// Whether a rect the program is about to clear, in its own text cells,
  /// meets the panel. Static because the region-clear point wants to
  /// answer it before it has decided to touch anything.
  [[nodiscard]] static bool rect_meets_panel(std::uint8_t bottom,
                                             std::uint8_t right,
                                             std::uint8_t top,
                                             std::uint8_t left) noexcept;

  // --- what this map looks like ----------------------------------------
  //
  // Two things the panel has to work out about a map before it can draw
  // it, both derived from what the program has in memory and neither of
  // them cheap enough to redo on every keyboard poll: **what colour each
  // kind of wall is**, histogrammed out of the very tiles the 3D view
  // blits for it, and **which kinds of wall are doors**. They are worked
  // out once when the party arrives and thrown away together.
  //
  // Keyed on the map *and* on which tile banks are loaded, because the
  // second is what the first is read out of: a wall set swapped under a
  // fixed map identity would otherwise keep the colours of the tiles it
  // replaced.

  /// Whether what is cached below belongs to this map and these banks.
  [[nodiscard]] bool appearance_is_for(std::uint8_t area, std::uint8_t geo,
                                       std::uint16_t banks) const noexcept;

  /// Whether it has been worked out for *any* map yet.
  ///
  /// For a host printing what the door rule decided (#268): "no door
  /// kinds on this map" and "the seam has never looked at a map" are the
  /// same two zero words, and a `--trace` line that could not tell them
  /// apart would be worse than none.
  [[nodiscard]] bool appearance_learned() const noexcept {
    return appearance_valid_;
  }

  /// Forget it and start again for this map and these banks.
  void begin_appearance(std::uint8_t area, std::uint8_t geo,
                        std::uint16_t banks) noexcept;

  /// The colour a wall face of this kind is drawn in, and whether one has
  /// been worked out. `nibble` is the map format's own wall-face number,
  /// 1 to 15; 0 is not a wall face.
  [[nodiscard]] bool wall_colour_known(unsigned nibble) const noexcept;
  [[nodiscard]] std::uint8_t wall_colour(unsigned nibble) const noexcept;
  void set_wall_colour(unsigned nibble, std::uint8_t colour) noexcept;

  /// Bit *n* set means a wall face of kind *n* is a door rather than a
  /// wall or an archway. Zero is "nothing on this map says which", which
  /// is a state the renderer has its own answer for.
  [[nodiscard]] std::uint16_t door_nibbles() const noexcept {
    return static_cast<std::uint16_t>(door_nibbles_seen_ | door_nibbles_table_);
  }

  /// The two sources of that mask, kept apart (#268).
  ///
  /// The renderer wants their union and nothing else — a door is a door
  /// whichever piece of evidence named it. What wants them apart is a
  /// person looking at a still and asking *why* a leaf is there: the
  /// first is this map's own shut faces, scanned on arrival, and the
  /// second is the shipped table of shut faces. Until #268 the two were
  /// added into one word on the way in, and every driven run to that
  /// point had been over a map with no shut face on it, so a leaf on a
  /// screenshot could not be told from a leaf the table had guessed.
  /// A host prints them (`--trace`), which is what makes a still
  /// evidence; nothing in this machine reads them apart.
  [[nodiscard]] std::uint16_t door_nibbles_seen() const noexcept {
    return door_nibbles_seen_;
  }
  [[nodiscard]] std::uint16_t door_nibbles_table() const noexcept {
    return door_nibbles_table_;
  }
  void set_door_nibbles(std::uint16_t seen, std::uint16_t from_table) noexcept {
    door_nibbles_seen_ = seen;
    door_nibbles_table_ = from_table;
  }

  /// The leaves the last draw drew, by evidence
  /// (`automap_door_evidence`). Started again at the top of each draw,
  /// so it always describes the panel as it stands.
  [[nodiscard]] const automap_door_tally& doors_drawn() const noexcept {
    return doors_drawn_;
  }
  void begin_doors_drawn() noexcept { doors_drawn_ = {}; }
  void count_door_drawn(automap_door_evidence why) noexcept;

  // --- the sidecar (M5-E2c) --------------------------------------------
  //
  // What has been seen is the half of this a player would be sorry to
  // lose, so a host persists it beside the save. Only the records go:
  // what the panel is doing, what colour this map's walls are and where
  // the party last stood are a session's business and are worked out
  // again in the first moments of the next one.

  /// How many bytes `write_sidecar` would fill for the records in hand.
  [[nodiscard]] std::size_t sidecar_bytes() const noexcept;

  /// The records, into `out`. Answers how many bytes were written, or
  /// zero if `out` is too small to hold them — which a caller finds out
  /// by asking `sidecar_bytes()` first, the way the rest of this tree's
  /// size-then-fill calls work.
  [[nodiscard]] std::size_t write_sidecar(
      std::span<std::uint8_t> out) const noexcept;

  /// Drop every record and keep everything else — what a host does when
  /// a save slot is loaded that has no sidecar beside it.
  ///
  /// Deliberately not `clear()`: the panel being open, and where the
  /// party was last believed to be, are the *session's* and have nothing
  /// to do with which map is remembered. A player who loads a save with
  /// the panel up should still have the panel up.
  void forget_records() noexcept;

  /// Read a sidecar back, **replacing** every record. False when the
  /// bytes are not a sidecar this build knows how to read, and then
  /// nothing has been touched: a refusal leaves what was already
  /// explored alone rather than dropping it.
  ///
  /// On success the serial moves and the panel's drawn signature is
  /// cleared, because the map on the screen is no longer the map in the
  /// store.
  [[nodiscard]] bool read_sidecar(std::span<const std::uint8_t> in) noexcept;

  // --- where the party was when it last stood still --------------------

  /// The last position that was trusted: the map identity and the cell.
  ///
  /// After a map change the party's coordinates are **stale** — the
  /// program's own arrival script has not placed the party yet, and the
  /// position words still hold the cell it left. Revealing then paints
  /// cells on the new map that nobody has been to. So the seam waits for
  /// the position to stop moving before it believes it, and this is what
  /// it believes.
  [[nodiscard]] bool settled() const noexcept { return settled_; }
  [[nodiscard]] automap_map_kind settled_kind() const noexcept {
    return settled_kind_;
  }
  [[nodiscard]] std::uint8_t settled_disk() const noexcept {
    return settled_disk_;
  }
  [[nodiscard]] std::uint8_t settled_area() const noexcept {
    return settled_area_;
  }
  [[nodiscard]] std::uint8_t settled_geo() const noexcept {
    return settled_geo_;
  }
  [[nodiscard]] std::uint8_t settled_x() const noexcept { return settled_x_; }
  [[nodiscard]] std::uint8_t settled_y() const noexcept { return settled_y_; }

  /// How many consecutive looks at the same cell it takes to believe it.
  static constexpr unsigned settle_looks = 3;

  /// One look at the live position, on the map it claims to be on.
  /// Answers whether the seam may now act on it — which is false while
  /// the position is still settling after a map change, and true from the
  /// moment it has held still.
  ///
  /// **The kind is part of the identity here too**, so that stepping off
  /// an overland map into an interior one is a map change and is waited
  /// out, exactly as a geometry-block swap is. The two seams that call
  /// this share one settling state, which is right: the party is in one
  /// place, and only one of the two screens is ever up.
  bool observe(automap_map_kind kind, std::uint8_t disk, std::uint8_t area,
               std::uint8_t geo, std::uint8_t x, std::uint8_t y) noexcept;

  bool observe(std::uint8_t disk, std::uint8_t area, std::uint8_t geo,
               std::uint8_t x, std::uint8_t y) noexcept {
    return observe(automap_map_kind::grid, disk, area, geo, x, y);
  }

  /// The overland's form: keyed (disk, area), no geometry block, and the
  /// row may be up to 35.
  bool observe_overland(std::uint8_t disk, std::uint8_t area, std::uint8_t x,
                        std::uint8_t y) noexcept {
    return observe(automap_map_kind::overland, disk, area, 0, x, y);
  }

  /// Forget the settled position without forgetting anything explored:
  /// what a map change and a loaded save both want.
  void unsettle() noexcept;

  // --- the panel's pixels ---------------------------------------------

  /// The rendered panel, one EGA palette index per pixel, row-major.
  ///
  /// It is here rather than a file-scope buffer in the seam so that it
  /// belongs to a machine, and so that a renderer test can look at what
  /// was drawn without a screen to look at. It is rebuilt whenever
  /// `signature()` moves and blitted into the planes from here.
  [[nodiscard]] std::array<std::uint8_t, automap_panel_pixels>&
  pixels() noexcept {
    return pixels_;
  }
  [[nodiscard]] const std::array<std::uint8_t, automap_panel_pixels>& pixels()
      const noexcept {
    return pixels_;
  }

  /// What the panel was drawn from last time, so an unchanged screen
  /// costs a comparison rather than ten thousand bus cycles. Zero means
  /// "nothing has been drawn", which no real signature is.
  [[nodiscard]] std::uint32_t drawn_signature() const noexcept {
    return drawn_signature_;
  }
  void set_drawn_signature(std::uint32_t value) noexcept {
    drawn_signature_ = value;
  }

  /// What the **explored overlay** last drew from (#179), and the reason
  /// it needs one of its own.
  ///
  /// That seam paints at two points: the return of the program's own
  /// present, where the program has just wiped whatever was there, and
  /// the keyboard poll, where nothing has been wiped and a repaint on
  /// every pass would be thousands of them a second. The second is what
  /// this is for — it is the difference between "something changed"
  /// (the party moved, a cell was revealed, a saved slot's table was
  /// read in) and "the program is polling".
  ///
  /// Zero means "nothing has been drawn", which no real signature is; a
  /// pass on which the overworld is not on the screen sets it back to
  /// zero, so coming back to the same square later still draws.
  [[nodiscard]] std::uint32_t explored_signature() const noexcept {
    return explored_signature_;
  }
  void set_explored_signature(std::uint32_t value) noexcept {
    explored_signature_ = value;
  }

  /// What the *fog* was last brought up to date from — where the party
  /// stood and which way it faced. Kept apart from the drawing signature
  /// because the two are asked at different times: the map keeps
  /// learning while the panel is closed, and the panel is redrawn for
  /// reasons the map does not care about (something painted over it).
  /// Without the split, a closed panel would re-walk the sight lines at
  /// every keyboard poll, which is thousands of times a second.
  [[nodiscard]] std::uint32_t revealed_signature() const noexcept {
    return revealed_signature_;
  }
  void set_revealed_signature(std::uint32_t value) noexcept {
    revealed_signature_ = value;
  }

 private:
  std::array<automap_record, max_records> records_{};
  std::size_t next_slot_{0};
  std::uint32_t serial_{0};

  bool panel_open_{false};
  bool panel_on_screen_{false};
  bool panel_covered_{false};
  bool at_command_bar_{false};

  bool appearance_valid_{false};
  std::uint8_t appearance_area_{};
  std::uint8_t appearance_geo_{};
  std::uint16_t appearance_banks_{};
  std::array<std::uint8_t, 16> wall_colour_{};
  std::array<bool, 16> wall_colour_known_{};
  std::uint16_t door_nibbles_seen_{};
  std::uint16_t door_nibbles_table_{};
  automap_door_tally doors_drawn_{};

  bool settled_{false};
  automap_map_kind settled_kind_{automap_map_kind::grid};
  std::uint8_t settled_disk_{};
  std::uint8_t settled_area_{};
  std::uint8_t settled_geo_{};
  std::uint8_t settled_x_{};
  std::uint8_t settled_y_{};

  std::uint8_t looking_x_{};
  std::uint8_t looking_y_{};
  unsigned looks_{0};
  bool pending_entrance_{false};

  std::array<std::uint8_t, automap_panel_pixels> pixels_{};
  std::uint32_t drawn_signature_{0};
  std::uint32_t revealed_signature_{0};
  std::uint32_t explored_signature_{0};
};

}  // namespace amberfolio::machine
