// SPDX-License-Identifier: AGPL-3.0-only
//
// What the automap panel and the scene read share about an interior map:
// the format of the program's four map planes, which faces are doors, what
// the party can see from where it stands, and what a map is called.
//
// **Why it is here and not in `seam_automap.cpp`.** The panel draws these
// answers and the scene read (`machine/scene.h`, #520) says them in words.
// A player who hears "door ahead" and a player who sees a yellow leaf ahead
// must be looking at the same answer, so the rule is one function both of
// them call rather than two that agree today.
//
// None of it reads the machine. The seam reads the map through the bus and
// the scene read reads it out of RAM; both hand the 1,024 bytes over here,
// with the three wall-set block numbers the door table is keyed on, and
// everything below is arithmetic on what it was handed.
//
// The facts are `docs/seams.md` §10's (the automap panel): addresses,
// offsets and a format description. No byte of the program is here.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/automap.h"

namespace amberfolio::machine::map_format {

// --- The map's format ------------------------------------------------------

/// The map is four 256-byte planes. Plane 0's high and low nibbles are the
/// north and east wall faces of cell `y * 16 + x`; plane 1's are south and
/// west; plane 3 holds two bits per face saying what kind of face it is.
inline constexpr std::uint16_t plane_bytes = 0x100;
inline constexpr std::uint16_t map_bytes = 4 * plane_bytes;
inline constexpr std::uint16_t faces_ns = 0;
inline constexpr std::uint16_t faces_sw = plane_bytes;
inline constexpr std::uint16_t face_styles = 3 * plane_bytes;

using grid = std::array<std::uint8_t, map_bytes>;

/// The four wall faces of a cell, in the order the format numbers them.
/// The same numbers the party's facing byte uses, which is what makes
/// "the face ahead of the party" a lookup rather than a conversion.
inline constexpr unsigned lane_north = 0;
inline constexpr unsigned lane_east = 2;
inline constexpr unsigned lane_south = 4;
inline constexpr unsigned lane_west = 6;
inline constexpr std::array<unsigned, 4> lanes{lane_north, lane_east,
                                               lane_south, lane_west};

/// One step in each lane's direction. A property of the format's own
/// numbering, above, and confirmed against the facing values recorded in
/// `docs/playable.md`: y grows southward.
struct step {
  int dx;
  int dy;
};
[[nodiscard]] constexpr step step_of(unsigned lane) noexcept {
  switch (lane & 7U) {
    case lane_north:
      return {.dx = 0, .dy = -1};
    case lane_east:
      return {.dx = 1, .dy = 0};
    case lane_south:
      return {.dx = 0, .dy = 1};
    default:
      return {.dx = -1, .dy = 0};
  }
}

/// The lane on the other side of a border.
[[nodiscard]] constexpr unsigned opposite(unsigned lane) noexcept {
  return (lane + 4U) & 7U;
}

/// What plane 3's two bits say about a face that exists at all: solid, a
/// way through, or a door that is shut (2 and 3 both). Which *passable*
/// faces are doors is not in this grid at all — see `door_kinds()`.
inline constexpr std::uint8_t face_solid = 0;
inline constexpr std::uint8_t face_passable = 1;
inline constexpr std::uint8_t face_shut_door = 2;

/// The grid's side as a signed number, because every coordinate that has
/// to be bounds-checked is one a step could have taken off the edge.
inline constexpr int map_side = static_cast<int>(automap_map_side);

[[nodiscard]] constexpr bool on_map(int x, int y) noexcept {
  return x >= 0 && x < map_side && y >= 0 && y < map_side;
}

[[nodiscard]] unsigned cell_of(unsigned x, unsigned y) noexcept;

/// The wall-face nibble of one lane of one cell. Zero is "no face here".
[[nodiscard]] std::uint8_t face_of(const grid& map, unsigned x, unsigned y,
                                   unsigned lane) noexcept;

/// The two style bits of one lane of one cell. Only meaningful where the
/// face nibble above is non-zero.
[[nodiscard]] std::uint8_t style_of(const grid& map, unsigned x, unsigned y,
                                    unsigned lane) noexcept;

// --- Which wall faces are doors (M5-E2a) -----------------------------------

/// What `wall_set_block` answers when the loader filled a slot from a
/// multi-block load and cannot say which block a row came from — the same
/// value the loader itself stamps.
inline constexpr std::uint16_t wall_set_unknown = 0xFFFF;

/// The wall-set descriptor in the data segment: which WALLDEF block is
/// loaded in each of the three slots, a word per slot at a stride of four
/// from this offset (slot 1 at `+4`). Slot 1 serves wall faces 1 to 5,
/// slot 2 serves 6 to 10, slot 3 serves 11 to 15.
inline constexpr std::uint16_t data_wall_set = 0x6AAE;
inline constexpr std::uint16_t wall_set_stride = 4;
inline constexpr unsigned wall_set_slots = 3;
inline constexpr unsigned wall_faces_per_slot = 5;

/// The three slots' block numbers as the program keeps them, slot 1 first.
using wall_set_blocks = std::array<std::uint16_t, wall_set_slots>;

/// The block serving wall face `nibble`, or `wall_set_unknown` — for a
/// nibble that is not a wall face, and for a slot whose word is not a
/// block number.
[[nodiscard]] std::uint16_t block_serving(const wall_set_blocks& blocks,
                                          std::uint8_t nibble) noexcept;

/// The two sources' answers, kept apart (#268): kinds seen shut on this
/// map, and kinds the shipped table names for the wall sets it has
/// loaded. A door is a door whichever named it; what wants them apart is
/// a person asking why a leaf is there.
struct door_evidence {
  std::uint16_t seen{};
  std::uint16_t from_table{};

  [[nodiscard]] std::uint16_t either() const noexcept {
    return static_cast<std::uint16_t>(seen | from_table);
  }
};

/// Which wall faces of this map are doors: its own shut faces, and the
/// table's answer for the wall sets loaded with it.
[[nodiscard]] door_evidence door_kinds(const grid& map, std::uint8_t disk,
                                       const wall_set_blocks& blocks) noexcept;

/// What one border between two cells is, as the panel draws it and the
/// scene read says it.
enum class border_kind : std::uint8_t {
  /// No face on either side: nothing there.
  open,
  /// A face that is solid.
  wall,
  /// A face that is a way through and that no rule calls a door.
  passage,
  /// A door, shut or not.
  door,
};

struct border {
  border_kind kind{border_kind::open};
  /// Whether a door is shut. Only ever true for a door.
  bool shut{false};
  /// The wall-face nibble that decides the border's look: the near face
  /// when there is one, else the far one. Zero for an open border.
  std::uint8_t face{};
  /// Which of the four rules made it a door (#268). Only meaningful for
  /// a door.
  automap_door_evidence why{automap_door_evidence::shut};
};

/// The border on `lane` of cell (x, y), from both sides of it.
///
/// A border's wall is often recorded on one side only, so a cell with no
/// face of its own borrows the far cell's. The near face decides when
/// there is one, exactly as the program's own movement and 3D view
/// decide, with one exception: a **shut** door on the far side is a shut
/// door, because being shut is a property of the border rather than of a
/// side. An off-map neighbour contributes nothing — the coordinates do
/// not wrap here, because a map whose edge borrowed the opposite edge's
/// walls would describe a border nobody can stand at.
///
/// `doors` is `door_kinds().either()` and `seen` its `seen` half.
[[nodiscard]] border border_of(const grid& map, unsigned x, unsigned y,
                               unsigned lane, std::uint16_t doors,
                               std::uint16_t seen) noexcept;

// --- What the party can see ------------------------------------------------

/// How far ahead sight carries: the cell the party is on and two more,
/// which is what the 3D view in front of it shows.
inline constexpr unsigned sight_depth = 3;

/// The most cells one look can see: at each depth the cell ahead and the
/// two beside it.
inline constexpr std::size_t sight_cells_max = std::size_t{sight_depth} * 3;

struct sight_cell {
  std::uint8_t x{};
  std::uint8_t y{};
};

/// The cells the party sees from (x, y) facing `facing` (a lane): its own
/// cell, the cells beside it that nothing blocks, and the same again for
/// as far ahead as sight carries. Into `out`, nearest first, the cells at
/// each depth in the order ahead, left, right; answers how many.
///
/// Sight crosses a border only where **both** faces are absent. Absent,
/// not passable: a doorway that is permanently open in the data is still
/// something the party has to walk through before it can say what is on
/// the other side, and seeing through them revealed the insides of
/// buildings from the street. Both faces, not the near one: the data
/// often records a border's wall on one side only.
///
/// Sight does not wrap. The program's coordinates do — the grid is a
/// torus — but a view that wrapped would name disconnected cells on the
/// opposite edge.
std::size_t sight_from(const grid& map, unsigned x, unsigned y, unsigned facing,
                       std::span<sight_cell, sight_cells_max> out) noexcept;

// --- What a map is called (M5-E2b) -----------------------------------------

/// Where `zone_label` puts the name of a map the table has no row for.
inline constexpr std::size_t zone_fallback_bytes = 8;
using zone_fallback = std::array<char, zone_fallback_bytes>;

/// `|` in a label is a soft break: where the panel's eight-column band may
/// break a word too wide for it. It is not a character of the name.
inline constexpr char soft_break = '|';

/// The label for a map, or `AREA <n>` where the table has no row, written
/// into `fallback` so the view always names storage that outlives it.
/// Where the party stands on the loaded map, for a map that holds more
/// than one place: the wilderness areas' grids are each several lairs,
/// told apart by the walls between them. No map (the overland) is a
/// place nowhere on a grid.
struct zone_place {
  const grid* map{};
  std::uint8_t x{};
  std::uint8_t y{};
};

/// `geo` is the loaded geometry block, which names the few sub-maps that
/// are a place of their own; pass zero where there is none (the overland).
[[nodiscard]] std::string_view zone_label(std::uint8_t disk, std::uint8_t area,
                                          std::uint8_t geo,
                                          const zone_place& where,
                                          zone_fallback& fallback) noexcept;

}  // namespace amberfolio::machine::map_format
