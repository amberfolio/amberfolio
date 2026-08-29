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

/// One bit per cell.
inline constexpr std::size_t automap_seen_bytes = automap_map_cells / 8;

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
inline constexpr std::uint8_t automap_sidecar_version = 1;
inline constexpr std::array<char, 3> automap_sidecar_magic{'A', 'F', 'M'};

/// One record on disk, fixed width, little-endian where it is wider than
/// a byte. Fixed rather than packed: a table of at most sixty-four maps
/// is four and a half kilobytes at its very largest, and a format a
/// reader can index into is worth more than the bytes.
///
///     0   1   the disk
///     1   1   the area
///     2   1   the geometry block
///     3   1   how many marks are live
///     4   32  one bit per cell, `y * 16 + x`, low bit of a byte first
///     36  12  the marks' x
///     48  12  the marks' y
///     60  12  the marks' kind
inline constexpr std::size_t automap_sidecar_record_bytes =
    4 + automap_seen_bytes + (3 * automap_max_markers);

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

  /// The map's identity, three bytes (see the header comment): the disk
  /// the area's files come from, the area id, and the loaded geometry
  /// block that distinguishes an in-place grid swap from the area it
  /// swapped inside.
  std::uint8_t disk{};
  std::uint8_t area{};
  std::uint8_t geo{};

  /// One bit per cell, `y * 16 + x`, low bit of a byte first.
  std::array<std::uint8_t, automap_seen_bytes> seen{};

  /// The marks, and how many of them are live.
  std::uint8_t marker_count{};
  std::array<std::uint8_t, automap_max_markers> marker_x{};
  std::array<std::uint8_t, automap_max_markers> marker_y{};
  std::array<automap_marker, automap_max_markers> marker_kind{};
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
  [[nodiscard]] automap_record& record_for(std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) noexcept;

  /// The record for this map identity, or null if none has been claimed.
  /// The const half, for a host that is persisting and a test that is
  /// asking.
  [[nodiscard]] const automap_record* find(std::uint8_t disk, std::uint8_t area,
                                           std::uint8_t geo) const noexcept;

  /// Whether a cell has been seen. Coordinates wrap at four bits, the way
  /// the program's own do.
  [[nodiscard]] static bool seen(const automap_record& map, unsigned x,
                                 unsigned y) noexcept;

  /// Mark a cell seen. True if it had not been.
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
    return door_nibbles_;
  }
  void set_door_nibbles(std::uint16_t mask) noexcept { door_nibbles_ = mask; }

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
  bool observe(std::uint8_t disk, std::uint8_t area, std::uint8_t geo,
               std::uint8_t x, std::uint8_t y) noexcept;

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
  std::uint16_t door_nibbles_{};

  bool settled_{false};
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
};

}  // namespace amberfolio::machine
