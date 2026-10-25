// SPDX-License-Identifier: AGPL-3.0-only
//
// The interior map's format, its door rule, its sight rule and its names
// (automap_faces.h), shared by the automap panel and the scene read.

#include "automap_faces.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/automap.h"

namespace amberfolio::machine::map_format {
namespace {

/// One WALLDEF block known to hold door graphics, and which of its five
/// rows they are on.
///
/// Facts about the shipped data — a disk number, a block number and a
/// bitmap of row indices — derived by sweeping every shut face in the
/// area files and asking which wall set was loaded for it. No byte of
/// anything is here and neither is any picture: this says *where* the
/// doors are, and the player's own copy says what they look like.
///
/// The durable identity of a wall graphic is (disk, WALLDEF block, row):
/// the same block reused in another slot or another area is the same
/// pictures. That is what the table is keyed on, and it is why it is a
/// fact table rather than a list of areas. It is consulted because the
/// evidence is scattered: five sub-maps of one castle share a wall set and
/// the shut instances are on three of them, so without it the other two
/// would call their doors archways.
struct door_graphics {
  std::uint8_t disk;
  std::uint16_t block;
  std::uint8_t rows;
};

constexpr std::array<door_graphics, 19> door_table{
    {{.disk = 1, .block = 1, .rows = 0x10},
     {.disk = 1, .block = 3, .rows = 0x10},
     {.disk = 2, .block = 1, .rows = 0x10},
     {.disk = 2, .block = 2, .rows = 0x10},
     {.disk = 2, .block = 4, .rows = 0x10},
     {.disk = 2, .block = 9, .rows = 0x01},
     {.disk = 2, .block = 19, .rows = 0x11},
     {.disk = 3, .block = 1, .rows = 0x10},
     {.disk = 3, .block = 3, .rows = 0x02},
     {.disk = 4, .block = 1, .rows = 0x10},
     {.disk = 4, .block = 3, .rows = 0x10},
     {.disk = 4, .block = 20, .rows = 0x10},
     {.disk = 4, .block = 22, .rows = 0x10},
     {.disk = 5, .block = 1, .rows = 0x10},
     {.disk = 5, .block = 24, .rows = 0x09},
     {.disk = 6, .block = 2, .rows = 0x10},
     {.disk = 6, .block = 6, .rows = 0x08},
     {.disk = 7, .block = 1, .rows = 0x10},
     {.disk = 8, .block = 18, .rows = 0x08}}};

/// Is this face a door?
///
/// A shut one always is. A passable one is a door when its nibble is a
/// door graphic — and when *nothing at all* is known about this map's
/// wall sets, every passable face is a door, which is the pre-nibble rule
/// and is better than calling none.
[[nodiscard]] bool is_door(std::uint8_t face, std::uint8_t style,
                           std::uint16_t doors) noexcept {
  if (face == 0 || style == face_solid) {
    return false;
  }
  if (style >= face_shut_door) {
    return true;
  }
  if (doors == 0) {
    return true;
  }
  return ((doors >> face) & 1U) != 0;
}

/// Which of the four rules made this face a door (#268). Called only where
/// `is_door` has already said yes, so it is a classification and not a
/// second decision.
[[nodiscard]] automap_door_evidence evidence_for(std::uint8_t face,
                                                 std::uint8_t style,
                                                 std::uint16_t doors,
                                                 std::uint16_t seen) noexcept {
  if (style >= face_shut_door) {
    return automap_door_evidence::shut;
  }
  if (doors == 0) {
    return automap_door_evidence::no_evidence;
  }
  return ((seen >> face) & 1U) != 0 ? automap_door_evidence::seen_kind
                                    : automap_door_evidence::table_kind;
}

/// Whether sight crosses a border: both faces absent (`sight_from`).
[[nodiscard]] bool sight_crosses(const grid& map, unsigned x, unsigned y,
                                 unsigned lane, unsigned nx,
                                 unsigned ny) noexcept {
  return face_of(map, x, y, lane) == 0 &&
         face_of(map, nx, ny, opposite(lane)) == 0;
}

/// A friendly name for a map, by the two bytes the program tells one from
/// another with: the disk its files come from and the area's script id.
///
/// **A name is a fact** (CONTRIBUTING.md: "a SHA-256, a name, and the
/// offsets a fact table needs"). This is a table of short labels for
/// places, of the same kind as the addresses and the door table beside
/// it, and not a line of anybody's prose. It has to be a table because
/// the program holds no such string anywhere to read: the names live in
/// its scripts as narration, and each of these was derived from one.
/// Carried from the proven design, which is what PLAN.md §5 means by
/// re-expressing it as-is.
///
/// The id is the *script* id, and a sub-map that a script swaps in under
/// a fixed area keeps its parent's — so most of those need no row of their
/// own. The third byte of the map identity is what tells them apart for
/// the fog (`automap.h`), and usually to a player they are the same place.
/// Where they are not, `sub_map_names` names the sub-map by that byte.
///
/// A `|` is a soft break (`soft_break`): a point inside a word where the
/// panel may split the label across two lines of its band with a hyphen.
/// Six of these have a word longer than the band is wide, and each
/// carries one at a syllable.
struct zone_name {
  std::uint8_t disk;
  std::uint8_t area;
  std::string_view name;
};

constexpr std::array<zone_name, 29> zone_names{
    {{.disk = 1, .area = 18, .name = "PODOL PLAZA"},
     {.disk = 1, .area = 24, .name = "TEMPLE OF BANE"},
     {.disk = 2, .area = 9, .name = "STOJANOW GATE"},
     {.disk = 2, .area = 15, .name = "MENDOR'S LIBRARY"},
     {.disk = 2, .area = 20, .name = "SLUMS"},
     {.disk = 3, .area = 0, .name = "NEW PHLAN"},
     {.disk = 3, .area = 8, .name = "CITY HALL"},
     {.disk = 3, .area = 11, .name = "TRAINING HALL"},
     {.disk = 3, .area = 14, .name = "KOVEL MANSION"},
     {.disk = 4, .area = 2, .name = "TEXTILE HOUSE"},
     {.disk = 4, .area = 10, .name = "VALHIN|GEN GRAVE|YARD"},
     {.disk = 4, .area = 21, .name = "SOKAL KEEP"},
     {.disk = 5, .area = 3, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 4, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 5, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 6, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 7, .name = "VALJEVO CASTLE"},
     {.disk = 6, .area = 1, .name = "BUCCA|NEER BASE"},
     {.disk = 6, .area = 19, .name = "DRAGON'S CAVE"},
     {.disk = 6, .area = 25, .name = "WILDER|NESS"},
     {.disk = 6, .area = 28, .name = "ZHENTIL OUTPOST"},
     {.disk = 7, .area = 17, .name = "NOMAD CAMP"},
     {.disk = 7, .area = 22, .name = "SORCER|ER'S ISLAND"},
     {.disk = 7, .area = 23, .name = "YARASH'S PYRAMID"},
     {.disk = 7, .area = 26, .name = "WILDER|NESS"},
     {.disk = 8, .area = 13, .name = "KOBOLD CAVES"},
     {.disk = 8, .area = 16, .name = "LIZARD|MAN KEEP"},
     {.disk = 8, .area = 27, .name = "WILDER|NESS"},
     {.disk = 8, .area = 29, .name = "KUTO'S WELL"}}};

/// A sub-map that is a district of its own, by the geometry block its
/// parent's script loads. Looked up before `zone_names`.
///
/// The Temple of Bane's script (disk 1, area 24) loads either its own
/// block 24 or block 31, the Wealthy Area beside it, and the two are
/// separate districts of the city. The other swaps keep their parent's
/// name because they are its grounds or its cellar: block 30 is around
/// the Lizardman Keep and block 32 is under Kuto's Well.
struct sub_map_name {
  std::uint8_t disk;
  std::uint8_t area;
  std::uint8_t geo;
  std::string_view name;
};

constexpr std::array<sub_map_name, 1> sub_map_names{
    {{.disk = 1, .area = 24, .geo = 31, .name = "WEALTHY AREA"}}};

}  // namespace

unsigned cell_of(unsigned x, unsigned y) noexcept {
  return ((y & 0x0FU) * automap_map_side) + (x & 0x0FU);
}

std::uint8_t face_of(const grid& map, unsigned x, unsigned y,
                     unsigned lane) noexcept {
  const unsigned cell = cell_of(x, y);
  switch (lane & 7U) {
    case lane_north:
      return static_cast<std::uint8_t>(map[faces_ns + cell] >> 4U);
    case lane_east:
      return static_cast<std::uint8_t>(map[faces_ns + cell] & 0x0FU);
    case lane_south:
      return static_cast<std::uint8_t>(map[faces_sw + cell] >> 4U);
    default:
      return static_cast<std::uint8_t>(map[faces_sw + cell] & 0x0FU);
  }
}

std::uint8_t style_of(const grid& map, unsigned x, unsigned y,
                      unsigned lane) noexcept {
  const std::uint8_t byte = map[face_styles + cell_of(x, y)];
  return static_cast<std::uint8_t>((byte >> (lane & 6U)) & 0x03U);
}

std::uint16_t block_serving(const wall_set_blocks& blocks,
                            std::uint8_t nibble) noexcept {
  if (nibble < 1 || nibble > 15) {
    return wall_set_unknown;
  }
  const std::uint16_t block = blocks[(nibble - 1U) / wall_faces_per_slot];
  return block > 0xFF ? wall_set_unknown : block;
}

door_evidence door_kinds(const grid& map, std::uint8_t disk,
                         const wall_set_blocks& blocks) noexcept {
  door_evidence evidence;
  for (unsigned y = 0; y < automap_map_side; ++y) {
    for (unsigned x = 0; x < automap_map_side; ++x) {
      for (const unsigned lane : lanes) {
        const std::uint8_t face = face_of(map, x, y, lane);
        if (face != 0 && style_of(map, x, y, lane) >= face_shut_door) {
          evidence.seen =
              static_cast<std::uint16_t>(evidence.seen | (1U << face));
        }
      }
    }
  }
  for (std::uint8_t nibble = 1; nibble <= 15; ++nibble) {
    const std::uint16_t block = block_serving(blocks, nibble);
    if (block == wall_set_unknown) {
      continue;
    }
    const auto row = static_cast<unsigned>((nibble - 1U) % wall_faces_per_slot);
    for (const door_graphics& known : door_table) {
      if (known.disk == disk && known.block == block &&
          ((known.rows >> row) & 1U) != 0) {
        evidence.from_table =
            static_cast<std::uint16_t>(evidence.from_table | (1U << nibble));
      }
    }
  }
  return evidence;
}

border border_of(const grid& map, unsigned x, unsigned y, unsigned lane,
                 std::uint16_t doors, std::uint16_t seen) noexcept {
  const std::uint8_t near_face = face_of(map, x, y, lane);
  const std::uint8_t near_style = style_of(map, x, y, lane);

  const step to = step_of(lane);
  const int nx = static_cast<int>(x & 0x0FU) + to.dx;
  const int ny = static_cast<int>(y & 0x0FU) + to.dy;
  std::uint8_t far_face = 0;
  std::uint8_t far_style = 0;
  if (on_map(nx, ny)) {
    const auto ux = static_cast<unsigned>(nx);
    const auto uy = static_cast<unsigned>(ny);
    far_face = face_of(map, ux, uy, opposite(lane));
    far_style = style_of(map, ux, uy, opposite(lane));
  }
  if (near_face == 0 && far_face == 0) {
    return {};
  }

  border found;
  found.face = near_face != 0 ? near_face : far_face;
  const bool far_shut = far_face != 0 && far_style >= face_shut_door;
  if (near_face != 0) {
    if (is_door(near_face, near_style, doors)) {
      found.kind = border_kind::door;
      found.why = evidence_for(near_face, near_style, doors, seen);
    } else if (far_shut) {
      // A solid or archway face against a shut door recorded only on the
      // far side: still a shut door, because shut belongs to the border.
      found.kind = border_kind::door;
      found.why = automap_door_evidence::shut;
    }
  } else if (is_door(far_face, far_style, doors)) {
    found.kind = border_kind::door;
    found.why = evidence_for(far_face, far_style, doors, seen);
  }
  if (found.kind == border_kind::door) {
    found.shut = found.why == automap_door_evidence::shut;
    return found;
  }
  const std::uint8_t style = near_face != 0 ? near_style : far_style;
  found.kind =
      style == face_passable ? border_kind::passage : border_kind::wall;
  return found;
}

std::size_t sight_from(const grid& map, unsigned x, unsigned y, unsigned facing,
                       std::span<sight_cell, sight_cells_max> out) noexcept {
  const unsigned ahead = facing & 7U;
  const unsigned left = (facing + 6U) & 7U;
  const unsigned right = (facing + 2U) & 7U;

  std::size_t count = 0;
  const auto see = [&out, &count](int cx, int cy) {
    out[count++] = {.x = static_cast<std::uint8_t>(cx),
                    .y = static_cast<std::uint8_t>(cy)};
  };

  auto cx = static_cast<int>(x & 0x0FU);
  auto cy = static_cast<int>(y & 0x0FU);
  for (unsigned depth = 0; depth < sight_depth; ++depth) {
    const auto ux = static_cast<unsigned>(cx);
    const auto uy = static_cast<unsigned>(cy);
    see(cx, cy);

    for (const unsigned side : {left, right}) {
      const step to = step_of(side);
      const int nx = cx + to.dx;
      const int ny = cy + to.dy;
      if (on_map(nx, ny) &&
          sight_crosses(map, ux, uy, side, static_cast<unsigned>(nx),
                        static_cast<unsigned>(ny))) {
        see(nx, ny);
      }
    }

    const step forward = step_of(ahead);
    const int nx = cx + forward.dx;
    const int ny = cy + forward.dy;
    if (!on_map(nx, ny) ||
        !sight_crosses(map, ux, uy, ahead, static_cast<unsigned>(nx),
                       static_cast<unsigned>(ny))) {
      break;
    }
    cx = nx;
    cy = ny;
  }
  return count;
}

std::string_view zone_label(std::uint8_t disk, std::uint8_t area,
                            std::uint8_t geo,
                            zone_fallback& fallback) noexcept {
  for (const sub_map_name& sub : sub_map_names) {
    if (sub.disk == disk && sub.area == area && sub.geo == geo) {
      return sub.name;
    }
  }
  for (const zone_name& zone : zone_names) {
    if (zone.disk == disk && zone.area == area) {
      return zone.name;
    }
  }

  std::size_t used = 0;
  for (const char ch : std::string_view{"AREA "}) {
    fallback[used++] = ch;
  }
  std::array<char, 3> digits{};
  std::size_t count = 0;
  auto value = static_cast<unsigned>(area);
  do {
    digits[count++] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  } while (value != 0);
  while (count > 0) {
    fallback[used++] = digits[--count];
  }
  return std::string_view{fallback.data(), used};
}

}  // namespace amberfolio::machine::map_format
