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

/// The game mode byte: 4 is the interior screen, 3 the travel view.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_travel = 3;
constexpr std::uint8_t mode_interior = 4;

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

  /// Where a far pointer in the data segment points, if `reach` bytes
  /// from there are inside conventional memory and inside its segment;
  /// zero, which is never such a place, when they are not. A pointer the
  /// program has not set up points anywhere.
  [[nodiscard]] std::uint32_t follow(std::uint16_t offset,
                                     std::uint32_t reach) const noexcept {
    const std::uint16_t off = data_word(offset);
    const std::uint16_t segment = data_word(offset + 2);
    const std::uint32_t base = (std::uint32_t{segment} * 16U) + off;
    if (segment == 0 || off + reach > 0x10000U || base + reach > ram_.size()) {
      return 0;
    }
    return base;
  }

 private:
  std::span<const std::uint8_t> ram_;
  std::uint32_t data_;
};

void name_zone(scene& out) noexcept {
  map_format::zone_fallback fallback{};
  const std::string_view label =
      map_format::zone_label(out.disk, out.area, fallback);
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
  name_zone(out);

  map_format::grid grid{};
  for (std::size_t i = 0; i < grid.size(); ++i) {
    grid[i] = ram.at(map_at + static_cast<std::uint32_t>(i));
  }
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
    const std::uint8_t geo = ram.at(record + record_geometry_block);
    const automap_record* explored = kept->find(out.disk, out.area, geo);
    if (explored != nullptr) {
      for (unsigned y = 0; y < automap_map_side; ++y) {
        for (unsigned x = 0; x < automap_map_side; ++x) {
          if (automap_state::seen(*explored, x, y)) {
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
  name_zone(out);

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
  json.put('[');
  for (unsigned i = 0; i < scene_window_side; ++i) {
    if (i != 0) {
      json.put(',');
    }
    json.put('[');
    for (unsigned j = 0; j < scene_window_side; ++j) {
      if (j != 0) {
        json.put(',');
      }
      const scene_cell& cell = from.window[(i * scene_window_side) + j];
      json.put('{');
      json.key("terrain");
      json.number(cell.terrain);
      json.put(',');
      json.key("move");
      json.string(scene_move_name(cell.move));
      json.put('}');
    }
    json.put(']');
  }
  json.put("]}");
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
  }
  return "unknown";
}

scene_trouble read_scene_in(std::span<const std::uint8_t> ram,
                            std::uint32_t data_base, const automap_state* kept,
                            scene& out) noexcept {
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
  return read_scene_in(box.memory().ram(),
                       seams.image_base() + where->dgroup_offset,
                       kept ? &box.automap() : nullptr, out);
}

std::size_t scene_json(const scene& from, std::span<char> out) noexcept {
  json_out json(out);
  if (from.screen == scene_screen::none) {
    json.put("null");
    return json.finish();
  }
  json.put('{');
  json.key("screen");
  json.string(from.screen == scene_screen::explore ? "explore" : "travel");
  json.put(',');
  json.key("disk");
  json.number(from.disk);
  json.put(',');
  json.key("area");
  json.number(from.area);
  json.put(',');
  json.key("zone");
  json.string(std::string_view{from.zone.data(), from.zone_length});
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
