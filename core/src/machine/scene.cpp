// SPDX-License-Identifier: AGPL-3.0-only
//
// The scene read (machine/scene.h, #520): the guards, the two screens, and
// the JSON a host is handed.

#include "amberfolio/machine/scene.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/seam.h"
#include "automap_faces.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts
// ---------------------------------------------------------------------------
//
// The automap's and the explored overlay's, restated for this read's own
// binary, which is the house pattern: a fact table belongs to the file
// that acts on it (`automap_overland.h`). Each has its second route where
// it was first written down: `docs/seams.md` §10 and
// `docs/explored-overlay.md` §2.

/// The baseline edition (edition.h), and only it.
struct scene_binary {
  std::string_view fingerprint;
  std::uint32_t dgroup_offset;
};

static_assert(scene_view_max == map_format::sight_cells_max);
static_assert(scene_map_max == automap_map_cells);

constexpr std::array<scene_binary, 1> binaries{{
    {.fingerprint =
         "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",
     .dgroup_offset = 0xC7C0},
}};

/// The game mode byte: 4 is the interior screen, 3 the travel view, 5 a
/// fight.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_travel = 3;
constexpr std::uint8_t mode_interior = 4;
constexpr std::uint8_t mode_combat = 5;

/// Which view is up: 1 the interior grid, 2 to 4 the three wilderness
/// areas.
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint8_t view_kind_grid = 1;
constexpr std::uint8_t view_kind_first_overland = 2;
constexpr std::uint8_t view_kind_last_overland = 4;

/// Non-zero while the program is running a scripted move between areas;
/// the position is not to be believed while it is.
constexpr std::uint16_t data_in_transition = 0x442F;

/// The disk the area's files come from, and the area's own id.
constexpr std::uint16_t data_disk_number = 0x5376;
constexpr std::uint16_t data_area_id = 0x84DC;

/// The party's square and facing everywhere but the overland.
constexpr std::uint16_t data_party_x = 0x6AAD;
constexpr std::uint16_t data_party_y = 0x6AAE;
constexpr std::uint16_t data_party_facing = 0x6AAF;

/// The loaded map: a far pointer, offset then segment.
constexpr std::uint16_t data_map_pointer = 0x6A5C;

/// The current area record: a far pointer, offset then segment. Inside
/// it, the loaded geometry block (the third byte of a map's identity), the
/// party's overland column and row (words), and the word that says the
/// program draws the wilderness in 3D instead.
constexpr std::uint16_t data_area_record = 0x49D2;
constexpr std::uint16_t record_geometry_block = 0x18A;
constexpr std::uint16_t record_overland_column = 0x186;
constexpr std::uint16_t record_overland_row = 0x188;
constexpr std::uint16_t record_shown_in_3d = 0x1CC;
constexpr std::uint16_t record_reach = record_shown_in_3d + 2;

/// The overland terrain table: 36 rows at a stride of 44, three sixteen-
/// column bands, one per wilderness area, at the per-view-kind column
/// bias in the byte table after it.
constexpr std::uint16_t data_terrain = 0x3648;
constexpr std::uint16_t terrain_stride = 0x2C;
constexpr std::uint16_t data_view_column_bias = 0x3C76;
constexpr int max_column_bias = terrain_stride - 1;

/// **What the travel view's move step does with a terrain number**
/// (overlay 14 rel `0x0818`): a set it never steps onto, a set it steps
/// onto only when the party can cross a river, and everything else. The
/// numbers are the step's own; the second route is a party on water that
/// does not walk (`docs/explored-overlay.md` §4).
struct terrain_range {
  std::uint8_t first;
  std::uint8_t last;
};

constexpr std::array<terrain_range, 9> blocked_terrain{
    {{.first = 0x4E, .last = 0x4E},
     {.first = 0x7E, .last = 0x80},
     {.first = 0x82, .last = 0x84},
     {.first = 0xA8, .last = 0xAC},
     {.first = 0xAE, .last = 0xB3},
     {.first = 0xBC, .last = 0xBC},
     {.first = 0xDD, .last = 0xDE},
     {.first = 0xE1, .last = 0xE9},
     {.first = 0xF5, .last = 0xF5}}};
constexpr std::array<terrain_range, 2> river_terrain{
    {{.first = 0x66, .last = 0x6C}, {.first = 0x72, .last = 0x72}}};

[[nodiscard]] bool in(std::span<const terrain_range> ranges,
                      std::uint8_t terrain) noexcept {
  return std::ranges::any_of(ranges, [terrain](const terrain_range& range) {
    return terrain >= range.first && terrain <= range.last;
  });
}

[[nodiscard]] scene_move move_onto(std::uint8_t terrain) noexcept {
  if (in(blocked_terrain, terrain)) {
    return scene_move::blocked;
  }
  return in(river_terrain, terrain) ? scene_move::river : scene_move::open;
}

// --- The tactical grid -----------------------------------------------------
//
// The setup writes these and the drawing reads them, which is each one's
// second route; the round loop and the move step are the others named.

/// The figures: a table of far pointers to their records, one-based, at a
/// stride of four, and beside it a table of their squares at the same
/// stride: column, row, the slot's own number, and its shape (zero: not on
/// the grid). Slot 0's shape byte is the number of slots.
constexpr std::uint16_t data_slot_records = 0x65B9;
constexpr std::uint16_t data_slot_squares = 0x5F27;
constexpr std::uint16_t slot_stride = 4;
constexpr std::uint16_t square_column = 0;
constexpr std::uint16_t square_row = 1;
constexpr std::uint16_t square_shape = 3;
constexpr std::uint16_t data_slot_count = data_slot_squares + square_shape;

/// The four shapes: one square, the square and the one below it, the
/// square and the one east of it, and two by two.
struct shape_size {
  std::uint8_t width;
  std::uint8_t height;
};
constexpr std::array<shape_size, 5> shapes{{{.width = 0, .height = 0},
                                            {.width = 1, .height = 1},
                                            {.width = 1, .height = 2},
                                            {.width = 2, .height = 1},
                                            {.width = 2, .height = 2}}};

/// Whose turn it is: a far pointer to the acting figure's record, which the
/// round loop writes and the spell aim reads.
constexpr std::uint16_t data_actor = 0x5D92;

/// A figure's record: a Pascal name of up to fifteen letters, the status
/// (the sheet's word), whether it is still in the fight, its side (0 the
/// party's), whether the computer gives its orders, the armour class as
/// the program keeps it (sixty less the AC the sheet prints), and the
/// current hit points.
constexpr std::uint16_t record_bytes = 0x11D;
constexpr std::uint16_t record_name = 0x00;
constexpr std::uint16_t record_status = 0x10C;
constexpr std::uint16_t record_in_fight = 0x10D;
constexpr std::uint16_t record_side = 0x10E;
constexpr std::uint16_t record_computer = 0x10F;
constexpr std::uint16_t record_ac = 0x111;
constexpr std::uint16_t record_hp = 0x11B;
constexpr int ac_base = 0x3C;

/// The fallen: a count, and seven bytes each in the table before it — the
/// record's far pointer, the column and row the body lies on, and the
/// ground it covers. The program adds one where a party member falls.
constexpr std::uint16_t data_body_count = 0x6715;
constexpr std::uint16_t data_bodies = 0x66D6;
constexpr std::uint16_t body_stride = 7;
constexpr std::uint16_t body_column = 4;
constexpr std::uint16_t body_row = 5;
constexpr std::uint8_t bodies_max = 8;

/// The fight's block: a far pointer to it; the window's top-left square at
/// `+2` and `+3`; the ground, a byte a square, fifty to a row, from `+7`.
/// The scroll keeps the window's top-left where the whole window is on the
/// grid.
constexpr std::uint16_t data_combat_block = 0x6716;
constexpr std::uint16_t block_window_x = 2;
constexpr std::uint16_t block_window_y = 3;
constexpr std::uint16_t block_ground = 7;
constexpr std::uint32_t block_bytes =
    block_ground + (scene_combat_columns * scene_combat_rows);
constexpr int window_x_max = scene_combat_columns - scene_combat_side;
constexpr int window_y_max = scene_combat_rows - scene_combat_side;

/// What a ground number costs to step onto, the first byte of four per
/// number: the move step refuses a square that costs more than the figure
/// has left, and 0xFF is more than any figure ever has. Ground 0 is off
/// the grid, where the move step asks whether to flee instead.
constexpr std::uint16_t data_ground_cost = 0x2744;
constexpr std::uint16_t ground_cost_stride = 4;
constexpr std::uint8_t cost_blocked = 0xFF;
constexpr std::uint8_t ground_off_grid = 0;

/// The aim bar's two frames while it waits for a key, from BP
/// (seam_aim_watch.cpp has both routes). From target to target: the
/// target's record, a far pointer, and the range it prints. Manual: the
/// cursor's column and row, the range word, and the record the panel
/// shows, a far pointer, zero for none.
constexpr int target_record = 0x0C;
constexpr int target_range = -0x30;
constexpr int manual_column = -0x33;
constexpr int manual_row = -0x34;
constexpr int manual_range = -0x38;
constexpr int manual_record = 0x14;

// ---------------------------------------------------------------------------
// Reading RAM
// ---------------------------------------------------------------------------

/// Conventional memory, read as storage and not as bus cycles, so nothing
/// the program can see moves. Every read is bounded; a read past the end
/// answers zero, which every guard below treats as "not set up".
class memory {
 public:
  memory(std::span<const std::uint8_t> ram, std::uint32_t data_base) noexcept
      : ram_(ram.first(
            std::min<std::size_t>(ram.size(), conventional_ram_size))),
        data_(data_base) {}

  [[nodiscard]] std::uint8_t at(std::uint32_t address) const noexcept {
    return address < ram_.size() ? ram_[address] : 0;
  }
  [[nodiscard]] std::uint16_t word_at(std::uint32_t address) const noexcept {
    return static_cast<std::uint16_t>(at(address) | (at(address + 1) << 8U));
  }
  [[nodiscard]] std::uint8_t data(std::uint16_t offset) const noexcept {
    return at(data_ + offset);
  }
  [[nodiscard]] std::uint16_t data_word(std::uint16_t offset) const noexcept {
    return word_at(data_ + offset);
  }
  /// Where a data-segment offset is in conventional memory.
  [[nodiscard]] std::uint32_t linear(std::uint16_t offset) const noexcept {
    return data_ + offset;
  }

  /// Where the far pointer stored at `address` points, if `reach` bytes
  /// from there fit in its segment and in conventional memory; zero if not.
  [[nodiscard]] std::uint32_t far_at(std::uint32_t address,
                                     std::uint32_t reach) const noexcept {
    const std::uint16_t off = word_at(address);
    const std::uint16_t segment = word_at(address + 2);
    const std::uint32_t base = (std::uint32_t{segment} * 16U) + off;
    if (segment == 0 || off + reach > 0x10000U || base + reach > ram_.size()) {
      return 0;
    }
    return base;
  }

  /// Where a far pointer in the data segment points, if `reach` bytes
  /// from there are inside conventional memory and inside its segment;
  /// zero, which is never such a place, when they are not. A pointer the
  /// program has not set up points anywhere.
  [[nodiscard]] std::uint32_t follow(std::uint16_t offset,
                                     std::uint32_t reach) const noexcept {
    return far_at(linear(offset), reach);
  }

 private:
  std::span<const std::uint8_t> ram_;
  std::uint32_t data_;
};

/// The loaded map, out of the program's buffer at `map_at`.
[[nodiscard]] map_format::grid read_grid(const memory& ram,
                                         std::uint32_t map_at) noexcept {
  map_format::grid grid{};
  for (std::size_t i = 0; i < grid.size(); ++i) {
    grid[i] = ram.at(map_at + static_cast<std::uint32_t>(i));
  }
  return grid;
}

/// The zone name, by the map's identity. `geo` is the loaded geometry
/// block, zero where there is none to read, and `where` the party's
/// square on the grid, none on the overland (`zone_label`).
void name_zone(scene& out, std::uint8_t geo,
               const map_format::zone_place& where) noexcept {
  map_format::zone_fallback fallback{};
  const std::string_view label =
      map_format::zone_label(out.disk, out.area, geo, where, fallback);
  std::size_t used = 0;
  for (const char ch : label) {
    if (ch != map_format::soft_break && used < out.zone.size()) {
      out.zone[used++] = ch;
    }
  }
  out.zone_length = static_cast<std::uint8_t>(used);
}

[[nodiscard]] scene_face face_word(const map_format::border& edge) noexcept {
  switch (edge.kind) {
    case map_format::border_kind::open:
      return scene_face::open;
    case map_format::border_kind::wall:
      return scene_face::wall;
    case map_format::border_kind::passage:
      return scene_face::opening;
    case map_format::border_kind::door:
      break;
  }
  return edge.shut ? scene_face::shut : scene_face::door;
}

[[nodiscard]] scene_square square_at(const map_format::grid& grid, unsigned x,
                                     unsigned y,
                                     const map_format::door_evidence& doors) {
  scene_square square{.x = static_cast<std::uint8_t>(x),
                      .y = static_cast<std::uint8_t>(y)};
  for (std::size_t i = 0; i < map_format::lanes.size(); ++i) {
    square.faces[i] = face_word(map_format::border_of(
        grid, x, y, map_format::lanes[i], doors.either(), doors.seen));
  }
  return square;
}

// ---------------------------------------------------------------------------
// The two screens
// ---------------------------------------------------------------------------

[[nodiscard]] bool read_explore(const memory& ram, const automap_state* kept,
                                scene& out) {
  if (ram.data(data_view_kind) != view_kind_grid) {
    return false;
  }
  const std::uint32_t record = ram.follow(data_area_record, record_reach);
  const std::uint32_t map_at =
      ram.follow(data_map_pointer, map_format::map_bytes);
  if (record == 0 || map_at == 0) {
    return false;
  }
  const std::uint8_t facing = ram.data(data_party_facing);
  if ((facing & 1U) != 0 || facing > scene_west) {
    // Only the four cardinals are a party facing.
    return false;
  }

  out.screen = scene_screen::explore;
  out.disk = ram.data(data_disk_number);
  out.area = ram.data(data_area_id);
  out.x = static_cast<std::uint8_t>(ram.data(data_party_x) & 0x0FU);
  out.y = static_cast<std::uint8_t>(ram.data(data_party_y) & 0x0FU);
  out.facing = facing;
  const std::uint8_t geo = ram.at(record + record_geometry_block);

  const map_format::grid grid = read_grid(ram, map_at);
  const map_format::zone_place where{.map = &grid, .x = out.x, .y = out.y};
  name_zone(out, geo, where);
  map_format::wall_set_blocks blocks{};
  for (unsigned slot = 0; slot < map_format::wall_set_slots; ++slot) {
    blocks[slot] = ram.data_word(static_cast<std::uint16_t>(
        map_format::data_wall_set +
        ((slot + 1U) * map_format::wall_set_stride)));
  }
  const map_format::door_evidence doors =
      map_format::door_kinds(grid, out.disk, blocks);

  std::array<map_format::sight_cell, map_format::sight_cells_max> seen{};
  const std::size_t count =
      map_format::sight_from(grid, out.x, out.y, out.facing, seen);
  for (std::size_t i = 0; i < count; ++i) {
    out.view[i] = square_at(grid, seen[i].x, seen[i].y, doors);
  }
  out.view_count = static_cast<std::uint8_t>(count);

  out.map_kept = kept != nullptr;
  if (kept != nullptr) {
    const automap_record* explored = kept->find(out.disk, out.area, geo);
    if (explored != nullptr) {
      // The panel's squares: only the lair the party is in.
      const map_format::place_squares here =
          map_format::place_of(out.disk, out.area, geo, where);
      for (unsigned y = 0; y < automap_map_side; ++y) {
        for (unsigned x = 0; x < automap_map_side; ++x) {
          if (here[map_format::cell_of(x, y)] &&
              automap_state::seen(*explored, x, y)) {
            out.map[out.map_count++] = square_at(grid, x, y, doors);
          }
        }
      }
    }
  }
  return true;
}

[[nodiscard]] bool read_travel(const memory& ram, scene& out) {
  const std::uint8_t kind = ram.data(data_view_kind);
  if (kind < view_kind_first_overland || kind > view_kind_last_overland) {
    return false;
  }
  const std::uint32_t record = ram.follow(data_area_record, record_reach);
  if (record == 0 || ram.word_at(record + record_shown_in_3d) != 0) {
    // With that word set the program draws these areas in the interior
    // view, with the view kind still saying wilderness.
    return false;
  }
  const std::uint16_t column = ram.word_at(record + record_overland_column);
  const std::uint16_t row = ram.word_at(record + record_overland_row);
  const int bias = ram.data(data_view_column_bias + kind);
  const std::uint8_t facing = ram.data(data_party_facing);
  if (column >= automap_overland_columns || row >= automap_overland_rows ||
      bias > max_column_bias || (facing & 1U) != 0 || facing > scene_west) {
    // The program clamps the position in its own move step, so one past
    // it means this record is not the one these offsets describe.
    return false;
  }

  out.screen = scene_screen::travel;
  out.disk = ram.data(data_disk_number);
  out.area = ram.data(data_area_id);
  out.x = static_cast<std::uint8_t>(column);
  out.y = static_cast<std::uint8_t>(row);
  out.facing = facing;
  name_zone(out, 0, {});

  const explored_window origin = explored_window_top_left(
      bias, static_cast<int>(column), static_cast<int>(row));
  out.window_x = origin.col - bias;
  out.window_y = origin.row;
  for (unsigned i = 0; i < scene_window_side; ++i) {
    for (unsigned j = 0; j < scene_window_side; ++j) {
      const auto offset = static_cast<std::uint16_t>(
          data_terrain +
          ((static_cast<unsigned>(origin.row) + i) * terrain_stride) +
          static_cast<unsigned>(origin.col) + j);
      const std::uint8_t terrain = ram.data(offset);
      out.window[(i * scene_window_side) + j] = {.terrain = terrain,
                                                 .move = move_onto(terrain)};
    }
  }
  return true;
}

void read_combatant(const memory& ram, std::uint32_t record,
                    scene_combatant& out) noexcept {
  const std::uint8_t length = std::min<std::uint8_t>(
      ram.at(record + record_name), static_cast<std::uint8_t>(scene_name_max));
  for (std::uint8_t i = 0; i < length; ++i) {
    out.name[i] = static_cast<char>(ram.at(record + record_name + 1U + i));
  }
  out.name_length = length;
  out.side =
      ram.at(record + record_side) == 0 ? scene_side::party : scene_side::enemy;
  out.player = ram.at(record + record_computer) == 0;
  out.standing = ram.at(record + record_in_fight) != 0;
  const std::uint8_t status = ram.at(record + record_status);
  out.status = status <= static_cast<std::uint8_t>(scene_status::gone)
                   ? static_cast<scene_status>(status)
                   : scene_status::okay;
  out.hp = ram.at(record + record_hp);
  out.ac = ac_base - static_cast<int>(ram.at(record + record_ac));
}

/// Where a figure not on the grid lies: a fallen party member's body,
/// which the screen draws where it fell.
void place_body(const memory& ram, std::uint32_t record,
                scene_combatant& out) noexcept {
  const std::uint8_t bodies = std::min(ram.data(data_body_count), bodies_max);
  for (std::uint8_t i = 0; i < bodies; ++i) {
    const auto body =
        static_cast<std::uint16_t>(data_bodies + (i * body_stride));
    if (ram.far_at(ram.linear(body), record_bytes) == record) {
      out.placed = true;
      out.x = ram.data(body + body_column);
      out.y = ram.data(body + body_row);
      out.width = 1;
      out.height = 1;
    }
  }
}

/// Whether any square of a figure is in the window: a large figure half in
/// it is drawn half in it.
[[nodiscard]] bool in_window(const scene& at,
                             const scene_combatant& who) noexcept {
  constexpr int side = scene_combat_side;
  return who.x + who.width > at.window_x && who.x < at.window_x + side &&
         who.y + who.height > at.window_y && who.y < at.window_y + side;
}

[[nodiscard]] scene_cell ground_at(const memory& ram, std::uint32_t block,
                                   unsigned x, unsigned y) noexcept {
  const std::uint8_t ground =
      ram.at(block + block_ground + (y * scene_combat_columns) + x);
  scene_cell cell{.terrain = ground};
  if (ground == ground_off_grid) {
    cell.move = scene_move::edge;
    return cell;
  }
  const std::uint8_t cost = ram.data(static_cast<std::uint16_t>(
      data_ground_cost + (ground * ground_cost_stride)));
  if (cost == cost_blocked) {
    cell.move = scene_move::blocked;
  } else {
    cell.cost = cost;
  }
  return cell;
}

/// The nearest of `who`'s squares to the nearest of `from`'s, by the aim
/// bar's measure.
[[nodiscard]] int range_between(const scene_combatant& from,
                                const scene_combatant& who) noexcept {
  int best = -1;
  for (unsigned i = 0; i < from.width; ++i) {
    for (unsigned j = 0; j < from.height; ++j) {
      for (unsigned k = 0; k < who.width; ++k) {
        for (unsigned l = 0; l < who.height; ++l) {
          const int r = scene_range(
              static_cast<int>(from.x + i), static_cast<int>(from.y + j),
              static_cast<int>(who.x + k), static_cast<int>(who.y + l));
          best = best < 0 ? r : std::min(best, r);
        }
      }
    }
  }
  return best;
}

/// Each figure's range from the acting one, and the nearest standing
/// figure on the other side from it.
void measure_from_actor(scene& out) noexcept {
  if (out.turn < 0) {
    return;
  }
  const scene_combatant& actor =
      out.combatants[static_cast<std::size_t>(out.turn)];
  if (!actor.placed) {
    return;
  }
  const auto turn = static_cast<std::size_t>(out.turn);
  for (std::size_t i = 0; i < out.combatant_count; ++i) {
    scene_combatant& who = out.combatants[i];
    if (i == turn || !who.placed) {
      continue;
    }
    who.range = range_between(actor, who);
    if (who.side != actor.side && who.standing &&
        (out.nearest < 0 ||
         who.range <
             out.combatants[static_cast<std::size_t>(out.nearest)].range)) {
      out.nearest = static_cast<int>(i);
    }
  }
}

/// A figure's record and its square on the grid, by slot.
struct slot_seen {
  std::uint32_t record;
  bool on_grid;
  std::uint8_t x;
  std::uint8_t y;
};

/// Where the aim bar is, out of the frame `at` names. Nothing is said
/// unless the frame holds what its facts describe: a cursor on the grid,
/// and from target to target, a target that is one of the figures.
void read_aim(const memory& ram, const scene_aim_frame& at,
              std::span<const slot_seen> slots,
              std::span<const std::uint32_t> listed, scene& out) {
  const std::uint32_t base = std::uint32_t{at.stack_segment} * 16U;
  const auto in_frame = [&](int offset) {
    return base + static_cast<std::uint16_t>(at.frame + offset);
  };
  const auto listed_as = [&](std::uint32_t record) {
    for (std::size_t i = 0; i < listed.size(); ++i) {
      if (record != 0 && listed[i] == record) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  scene_aim aim{.mode = at.mode, .target = at.offered};
  if (at.mode == scene_aim_mode::target) {
    const std::uint32_t target =
        ram.far_at(in_frame(target_record), record_bytes);
    const slot_seen* seen = nullptr;
    for (const slot_seen& slot : slots) {
      if (target != 0 && slot.record == target && slot.on_grid) {
        seen = &slot;
      }
    }
    if (seen == nullptr) {
      return;
    }
    aim.x = seen->x;
    aim.y = seen->y;
    aim.under = listed_as(target);
    if (at.range_shown) {
      aim.range = ram.at(in_frame(target_range));
    }
  } else if (at.mode == scene_aim_mode::manual) {
    aim.x = ram.at(in_frame(manual_column));
    aim.y = ram.at(in_frame(manual_row));
    if (aim.x >= scene_combat_columns || aim.y >= scene_combat_rows) {
      return;
    }
    aim.under = listed_as(ram.far_at(in_frame(manual_record), record_bytes));
    if (at.range_shown) {
      aim.range = ram.word_at(in_frame(manual_range));
    }
  } else {
    return;
  }
  out.aim = aim;
}

[[nodiscard]] bool read_combat(const memory& ram, const scene_aim_frame* aim,
                               scene& out) {
  const std::uint32_t block = ram.follow(data_combat_block, block_bytes);
  const std::uint8_t slots = ram.data(data_slot_count);
  if (block == 0 || slots == 0 || slots > scene_combatants_max) {
    return false;
  }
  // The program keeps them as signed bytes; a negative one reads as more
  // than either limit, and is refused with the rest.
  const int window_x = ram.at(block + block_window_x);
  const int window_y = ram.at(block + block_window_y);
  if (window_x > window_x_max || window_y > window_y_max) {
    return false;
  }

  out.screen = scene_screen::combat;
  out.disk = ram.data(data_disk_number);
  out.area = ram.data(data_area_id);
  const std::uint32_t area_record =
      ram.follow(data_area_record, record_geometry_block + 1);
  // A fight on the grid leaves the party's square and the map where they
  // were (the program builds the field from both), so a lair is named in
  // one; a fight on the overland has no square on a grid.
  const std::uint32_t map_at =
      ram.follow(data_map_pointer, map_format::map_bytes);
  map_format::grid grid{};
  map_format::zone_place where{};
  if (ram.data(data_view_kind) == view_kind_grid && map_at != 0) {
    grid = read_grid(ram, map_at);
    where = {.map = &grid,
             .x = static_cast<std::uint8_t>(ram.data(data_party_x) & 0x0FU),
             .y = static_cast<std::uint8_t>(ram.data(data_party_y) & 0x0FU)};
  }
  name_zone(out,
            area_record == 0 ? 0 : ram.at(area_record + record_geometry_block),
            where);
  out.window_x = window_x;
  out.window_y = window_y;

  const std::uint32_t actor = ram.follow(data_actor, record_bytes);
  std::array<slot_seen, scene_combatants_max> seen{};
  std::array<std::uint32_t, scene_combatants_max> listed{};
  for (unsigned slot = 1; slot <= slots; ++slot) {
    const std::uint32_t record = ram.follow(
        static_cast<std::uint16_t>(data_slot_records + (slot * slot_stride)),
        record_bytes);
    if (record == 0) {
      continue;
    }
    scene_combatant who{};
    read_combatant(ram, record, who);
    const auto squares =
        static_cast<std::uint16_t>(data_slot_squares + (slot * slot_stride));
    const std::uint8_t shape = ram.data(squares + square_shape);
    if (shape != 0 && shape < shapes.size()) {
      who.placed = true;
      who.x = ram.data(squares + square_column);
      who.y = ram.data(squares + square_row);
      who.width = shapes[shape].width;
      who.height = shapes[shape].height;
      seen[slot - 1] = {
          .record = record, .on_grid = true, .x = who.x, .y = who.y};
    } else if (who.side == scene_side::party) {
      place_body(ram, record, who);
    }
    if (who.x >= scene_combat_columns || who.y >= scene_combat_rows) {
      who.placed = false;
    }
    // The party's own wherever they are; an enemy only where the screen
    // shows one, which a fallen enemy never is.
    if (who.side == scene_side::enemy &&
        !(who.placed && who.standing && in_window(out, who))) {
      continue;
    }
    const bool acting = record == actor;
    who.health_shown = who.side == scene_side::party || acting;
    if (!who.health_shown) {
      who.hp = 0;
      who.ac = 0;
    }
    if (acting) {
      out.turn = out.combatant_count;
      out.waiting = who.player && who.standing;
    }
    listed[out.combatant_count] = record;
    out.combatants[out.combatant_count++] = who;
  }

  for (unsigned i = 0; i < scene_combat_side; ++i) {
    for (unsigned j = 0; j < scene_combat_side; ++j) {
      out.ground[(i * scene_combat_side) + j] =
          ground_at(ram, block, static_cast<unsigned>(window_x) + j,
                    static_cast<unsigned>(window_y) + i);
    }
  }
  measure_from_actor(out);
  out.aim_kept = aim != nullptr;
  if (aim != nullptr) {
    read_aim(ram, *aim, std::span(seen).first(slots),
             std::span(listed).first(out.combatant_count), out);
  }
  return true;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

/// Text into a caller's buffer, counted whether it fits or not.
class json_out {
 public:
  explicit json_out(std::span<char> out) noexcept : out_(out) {}

  void put(std::string_view text) noexcept {
    for (const char ch : text) {
      if (used_ < out_.size()) {
        out_[used_] = ch;
      }
      ++used_;
    }
  }
  void put(char ch) noexcept { put(std::string_view{&ch, 1}); }

  void number(int value) noexcept {
    if (value < 0) {
      put('-');
      value = -value;
    }
    std::array<char, 12> digits{};
    std::size_t count = 0;
    auto rest = static_cast<unsigned>(value);
    do {
      digits[count++] = static_cast<char>('0' + (rest % 10U));
      rest /= 10U;
    } while (rest != 0);
    while (count > 0) {
      put(digits[--count]);
    }
  }

  /// A string the scene made: letters, digits, spaces and apostrophes,
  /// so nothing in it needs escaping. Anything else is dropped rather
  /// than escaped, which none of the zone table's names exercises.
  void string(std::string_view text) noexcept {
    put('"');
    for (const char ch : text) {
      if (ch >= 0x20 && ch < 0x7F && ch != '"' && ch != '\\') {
        put(ch);
      }
    }
    put('"');
  }

  void key(std::string_view name) noexcept {
    string(name);
    put(':');
  }

  /// The length without the NUL, and the NUL when there is room for it.
  [[nodiscard]] std::size_t finish() noexcept {
    if (used_ < out_.size()) {
      out_[used_] = '\0';
    }
    return used_;
  }

 private:
  std::span<char> out_;
  std::size_t used_{0};
};

constexpr std::array<std::string_view, 4> face_keys{"north", "east", "south",
                                                    "west"};

void write_square(json_out& json, const scene_square& square) {
  json.put('{');
  json.key("x");
  json.number(square.x);
  json.put(',');
  json.key("y");
  json.number(square.y);
  for (std::size_t i = 0; i < square.faces.size(); ++i) {
    json.put(',');
    json.key(face_keys[i]);
    json.string(scene_face_name(square.faces[i]));
  }
  json.put('}');
}

void write_squares(json_out& json, std::span<const scene_square> squares) {
  json.put('[');
  for (std::size_t i = 0; i < squares.size(); ++i) {
    if (i != 0) {
      json.put(',');
    }
    write_square(json, squares[i]);
  }
  json.put(']');
}

void write_explore(json_out& json, const scene& from) {
  // The party's own square's faces again, turned to the party: what is
  // ahead, to the right, behind and to the left.
  const scene_square& here = from.view[0];
  json.put(',');
  json.key("around");
  json.put('{');
  constexpr std::array<std::string_view, 4> turns{"ahead", "right", "behind",
                                                  "left"};
  for (unsigned turn = 0; turn < turns.size(); ++turn) {
    if (turn != 0) {
      json.put(',');
    }
    json.key(turns[turn]);
    json.string(scene_face_name(
        here.face(static_cast<std::uint8_t>(from.facing + (turn * 2U)))));
  }
  json.put('}');

  json.put(',');
  json.key("view");
  write_squares(json, std::span(from.view).first(from.view_count));
  json.put(',');
  json.key("map");
  if (from.map_kept) {
    write_squares(json, std::span(from.map).first(from.map_count));
  } else {
    json.put("null");
  }
}

/// A window of cells, rows from the top, west to east; `cost` only where a
/// fight gives one.
void write_cells(json_out& json, std::span<const scene_cell> cells,
                 unsigned side) {
  json.put('[');
  for (unsigned i = 0; i < side; ++i) {
    if (i != 0) {
      json.put(',');
    }
    json.put('[');
    for (unsigned j = 0; j < side; ++j) {
      if (j != 0) {
        json.put(',');
      }
      const scene_cell& cell = cells[(i * side) + j];
      json.put('{');
      json.key("terrain");
      json.number(cell.terrain);
      json.put(',');
      json.key("move");
      json.string(scene_move_name(cell.move));
      if (cell.cost != 0) {
        json.put(',');
        json.key("cost");
        json.number(cell.cost);
      }
      json.put('}');
    }
    json.put(']');
  }
  json.put(']');
}

void write_travel(json_out& json, const scene& from) {
  json.put(',');
  json.key("window");
  json.put('{');
  json.key("x");
  json.number(from.window_x);
  json.put(',');
  json.key("y");
  json.number(from.window_y);
  json.put(',');
  json.key("rows");
  write_cells(json, from.window, scene_window_side);
  json.put('}');
}

void write_combatant(json_out& json, const scene_combatant& who) {
  json.put('{');
  json.key("name");
  json.string(std::string_view{who.name.data(), who.name_length});
  json.put(',');
  json.key("side");
  json.string(scene_side_name(who.side));
  json.put(',');
  json.key("player");
  json.put(who.player ? "true" : "false");
  json.put(',');
  json.key("standing");
  json.put(who.standing ? "true" : "false");
  json.put(',');
  json.key("status");
  json.string(scene_status_name(who.status));
  if (who.placed) {
    json.put(',');
    json.key("x");
    json.number(who.x);
    json.put(',');
    json.key("y");
    json.number(who.y);
    json.put(',');
    json.key("width");
    json.number(who.width);
    json.put(',');
    json.key("height");
    json.number(who.height);
  }
  if (who.range >= 0) {
    json.put(',');
    json.key("range");
    json.number(who.range);
  }
  if (who.health_shown) {
    json.put(',');
    json.key("hp");
    json.number(who.hp);
    json.put(',');
    json.key("ac");
    json.number(who.ac);
  }
  json.put('}');
}

/// An index into the combatants, or null for none.
void write_index(json_out& json, int index) {
  if (index < 0) {
    json.put("null");
  } else {
    json.number(index);
  }
}

/// The aim bar: absent while nothing watches it, null while it is not
/// waiting for a key.
void write_aim(json_out& json, const scene& from) {
  if (!from.aim_kept) {
    return;
  }
  json.put(',');
  json.key("aim");
  const scene_aim& aim = from.aim;
  if (aim.mode == scene_aim_mode::none) {
    json.put("null");
    return;
  }
  json.put('{');
  json.key("mode");
  json.string(scene_aim_mode_name(aim.mode));
  json.put(',');
  json.key("x");
  json.number(aim.x);
  json.put(',');
  json.key("y");
  json.number(aim.y);
  json.put(',');
  json.key("under");
  write_index(json, aim.under);
  json.put(',');
  json.key("range");
  if (aim.range < 0) {
    json.put("null");
  } else {
    json.number(aim.range);
  }
  json.put(',');
  json.key("target");
  json.put(aim.target ? "true" : "false");
  json.put('}');
}

void write_combat(json_out& json, const scene& from) {
  json.put(',');
  json.key("turn");
  write_index(json, from.turn);
  json.put(',');
  json.key("waiting");
  json.put(from.waiting ? "true" : "false");
  json.put(',');
  json.key("nearest");
  write_index(json, from.nearest);
  write_aim(json, from);
  json.put(',');
  json.key("combatants");
  json.put('[');
  for (std::size_t i = 0; i < from.combatant_count; ++i) {
    if (i != 0) {
      json.put(',');
    }
    write_combatant(json, from.combatants[i]);
  }
  json.put(']');
  json.put(',');
  json.key("window");
  json.put('{');
  json.key("x");
  json.number(from.window_x);
  json.put(',');
  json.key("y");
  json.number(from.window_y);
  json.put(',');
  json.key("rows");
  write_cells(json, from.ground, scene_combat_side);
  json.put('}');
}

}  // namespace

const char* scene_trouble_name(scene_trouble trouble) noexcept {
  switch (trouble) {
    case scene_trouble::none:
      return "none";
    case scene_trouble::no_program:
      return "no_program";
    case scene_trouble::wrong_binary:
      return "wrong_binary";
    case scene_trouble::elsewhere:
      return "elsewhere";
  }
  return "unknown";
}

const char* scene_face_name(scene_face face) noexcept {
  switch (face) {
    case scene_face::open:
      return "open";
    case scene_face::wall:
      return "wall";
    case scene_face::opening:
      return "opening";
    case scene_face::door:
      return "door";
    case scene_face::shut:
      return "shut";
  }
  return "unknown";
}

const char* scene_facing_name(std::uint8_t facing) noexcept {
  switch (facing) {
    case scene_north:
      return "north";
    case scene_east:
      return "east";
    case scene_south:
      return "south";
    case scene_west:
      return "west";
    default:
      return "unknown";
  }
}

const char* scene_move_name(scene_move move) noexcept {
  switch (move) {
    case scene_move::open:
      return "open";
    case scene_move::blocked:
      return "blocked";
    case scene_move::river:
      return "river";
    case scene_move::edge:
      return "edge";
  }
  return "unknown";
}

const char* scene_aim_mode_name(scene_aim_mode mode) noexcept {
  switch (mode) {
    case scene_aim_mode::none:
      return "none";
    case scene_aim_mode::target:
      return "target";
    case scene_aim_mode::manual:
      return "manual";
  }
  return "unknown";
}

const char* scene_side_name(scene_side side) noexcept {
  switch (side) {
    case scene_side::party:
      return "party";
    case scene_side::enemy:
      return "enemy";
  }
  return "unknown";
}

const char* scene_status_name(scene_status status) noexcept {
  switch (status) {
    case scene_status::okay:
      return "okay";
    case scene_status::animated:
      return "animated";
    case scene_status::temporarily_gone:
      return "temporarily_gone";
    case scene_status::running:
      return "running";
    case scene_status::unconscious:
      return "unconscious";
    case scene_status::dying:
      return "dying";
    case scene_status::dead:
      return "dead";
    case scene_status::stoned:
      return "stoned";
    case scene_status::gone:
      return "gone";
  }
  return "unknown";
}

scene_trouble read_scene_in(std::span<const std::uint8_t> ram,
                            std::uint32_t data_base, const automap_state* kept,
                            const scene_aim_frame* aim, scene& out) noexcept {
  out = {};
  const memory memory_now(ram, data_base);
  if (memory_now.data(data_in_transition) != 0) {
    return scene_trouble::elsewhere;
  }
  bool found = false;
  switch (memory_now.data(data_game_mode)) {
    case mode_interior:
      found = read_explore(memory_now, kept, out);
      break;
    case mode_travel:
      found = read_travel(memory_now, out);
      break;
    case mode_combat:
      found = read_combat(memory_now, aim, out);
      break;
    default:
      break;
  }
  if (!found) {
    out = {};
    return scene_trouble::elsewhere;
  }
  return scene_trouble::none;
}

scene_trouble read_scene(const machine& box, scene& out) noexcept {
  out = {};
  const seam_engine& seams = box.seams();
  if (!seams.have_program()) {
    return scene_trouble::no_program;
  }
  const scene_binary* where = nullptr;
  for (const scene_binary& known : binaries) {
    if (digest_is(seams.program(), known.fingerprint)) {
      where = &known;
    }
  }
  if (where == nullptr) {
    return scene_trouble::wrong_binary;
  }
  // The explored map is the automap's, and only while it is on is it
  // being kept: with it off, no cell is "explored" because nothing looked.
  const bool kept = seams.status("automap").state == seam_state::on;
  // The aim bar's frame is noted by `modern-controls`, and only while it is
  // on is anything watching the bar.
  scene_aim_frame aim{};
  const bool aim_kept = aim_watch_now(box, aim);
  return read_scene_in(
      box.memory().ram(), seams.image_base() + where->dgroup_offset,
      kept ? &box.automap() : nullptr, aim_kept ? &aim : nullptr, out);
}

std::size_t scene_json(const scene& from, std::span<char> out) noexcept {
  json_out json(out);
  if (from.screen == scene_screen::none) {
    json.put("null");
    return json.finish();
  }
  json.put('{');
  json.key("screen");
  json.string(from.screen == scene_screen::explore  ? "explore"
              : from.screen == scene_screen::travel ? "travel"
                                                    : "combat");
  json.put(',');
  json.key("disk");
  json.number(from.disk);
  json.put(',');
  json.key("area");
  json.number(from.area);
  json.put(',');
  json.key("zone");
  json.string(std::string_view{from.zone.data(), from.zone_length});
  if (from.screen == scene_screen::combat) {
    // A fight has no one square for the party: each figure has its own.
    write_combat(json, from);
    json.put('}');
    return json.finish();
  }
  json.put(',');
  json.key("x");
  json.number(from.x);
  json.put(',');
  json.key("y");
  json.number(from.y);
  json.put(',');
  json.key("facing");
  json.string(scene_facing_name(from.facing));
  if (from.screen == scene_screen::explore) {
    write_explore(json, from);
  } else {
    write_travel(json, from);
  }
  json.put('}');
  return json.finish();
}

}  // namespace amberfolio::machine
