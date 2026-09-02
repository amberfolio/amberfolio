// SPDX-License-Identifier: AGPL-3.0-only
//
// `automap_state` (automap.h): the exploration store, and the settling
// rule that decides when a position may be believed.
//
// Nothing here reads the machine. The seam does the reading and hands the
// answers over, which is what keeps this file testable against nothing at
// all — and what makes the difference between "the store is wrong" and
// "the facts are wrong" a difference a test can tell.

#include "amberfolio/machine/automap.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {

std::size_t automap_cell_index(automap_map_kind kind, unsigned x,
                               unsigned y) noexcept {
  if (kind == automap_map_kind::overland) {
    if (x >= automap_overland_columns || y >= automap_overland_rows) {
      return automap_no_cell;
    }
    return (static_cast<std::size_t>(y) * automap_overland_columns) + x;
  }
  return ((static_cast<std::size_t>(y) & 0x0FU) * automap_map_side) +
         (x & 0x0FU);
}

namespace {

[[nodiscard]] constexpr int clamped(int value, int low, int high) noexcept {
  if (value < low) {
    return low;
  }
  return value > high ? high : value;
}

/// The two clamps the program's own composer applies, spelled out of the
/// arithmetic rather than out of any helper of this project's.
constexpr int explored_max_col = 0x27;
constexpr int explored_max_row = 0x1F;

}  // namespace

explored_window explored_window_top_left(int bias, int column,
                                         int row) noexcept {
  return {.col = clamped(bias + column - 2, 0, explored_max_col),
          .row = clamped(row - 2, 0, explored_max_row)};
}

void automap_state::clear() noexcept {
  records_ = {};
  next_slot_ = 0;
  serial_ = 0;
  panel_open_ = false;
  panel_on_screen_ = false;
  panel_covered_ = false;
  at_command_bar_ = false;
  unsettle();
  settled_kind_ = automap_map_kind::grid;
  appearance_valid_ = false;
  appearance_area_ = 0;
  appearance_geo_ = 0;
  appearance_banks_ = 0;
  wall_colour_ = {};
  wall_colour_known_ = {};
  door_nibbles_ = 0;
  pixels_ = {};
  drawn_signature_ = 0;
  revealed_signature_ = 0;
  explored_signature_ = 0;
}

automap_record& automap_state::record_for(automap_map_kind kind,
                                          std::uint8_t disk, std::uint8_t area,
                                          std::uint8_t geo) noexcept {
  for (automap_record& map : records_) {
    if (map.used && map.kind == kind && map.disk == disk && map.area == area &&
        map.geo == geo) {
      return map;
    }
  }
  for (automap_record& map : records_) {
    if (!map.used) {
      map = {};
      map.used = true;
      map.kind = kind;
      map.disk = disk;
      map.area = area;
      map.geo = geo;
      ++serial_;
      return map;
    }
  }

  // Full. Reuse in claim order rather than refusing: the map the party is
  // standing on has to be recordable, and this is unreachable on the
  // program this build targets (automap.h).
  automap_record& map = records_[next_slot_];
  next_slot_ = (next_slot_ + 1) % max_records;
  map = {};
  map.used = true;
  map.kind = kind;
  map.disk = disk;
  map.area = area;
  map.geo = geo;
  ++serial_;
  return map;
}

const automap_record* automap_state::find(automap_map_kind kind,
                                          std::uint8_t disk, std::uint8_t area,
                                          std::uint8_t geo) const noexcept {
  for (const automap_record& map : records_) {
    if (map.used && map.kind == kind && map.disk == disk && map.area == area &&
        map.geo == geo) {
      return &map;
    }
  }
  return nullptr;
}

bool automap_state::seen(const automap_record& map, unsigned x,
                         unsigned y) noexcept {
  const std::size_t index = automap_cell_index(map.kind, x, y);
  if (index == automap_no_cell) {
    return false;
  }
  return ((map.seen[index / 8] >> (index % 8)) & 1U) != 0;
}

bool automap_state::reveal(automap_record& map, unsigned x,
                           unsigned y) noexcept {
  const std::size_t index = automap_cell_index(map.kind, x, y);
  if (index == automap_no_cell) {
    return false;
  }
  const auto bit = static_cast<std::uint8_t>(1U << (index % 8));
  if ((map.seen[index / 8] & bit) != 0) {
    return false;
  }
  map.seen[index / 8] = static_cast<std::uint8_t>(map.seen[index / 8] | bit);
  ++serial_;
  return true;
}

void automap_state::mark(automap_record& map, unsigned x, unsigned y,
                         automap_marker kind) noexcept {
  if (kind == automap_marker::none) {
    return;
  }
  if (map.kind != automap_map_kind::grid) {
    // The overland carries no marks. Nothing draws them there — the
    // explored overlay (#179) has no panel to put a way-in glyph on — and
    // the marker coordinates are four-bit, which a row of 35 is not. A
    // silent wrap would put a mark on a cell nobody has been to.
    return;
  }
  const auto cx = static_cast<std::uint8_t>(x & 0x0FU);
  const auto cy = static_cast<std::uint8_t>(y & 0x0FU);
  for (std::size_t i = 0; i < map.marker_count; ++i) {
    if (map.marker_x[i] == cx && map.marker_y[i] == cy) {
      // First kind wins. A cell that is both the way in and the way out
      // is the way in, which is the more useful of the two to see.
      return;
    }
  }
  if (map.marker_count >= automap_max_markers) {
    return;
  }
  map.marker_x[map.marker_count] = cx;
  map.marker_y[map.marker_count] = cy;
  map.marker_kind[map.marker_count] = kind;
  ++map.marker_count;
  ++serial_;
}

automap_marker automap_state::marker_at(const automap_record& map, unsigned x,
                                        unsigned y) noexcept {
  const auto cx = static_cast<std::uint8_t>(x & 0x0FU);
  const auto cy = static_cast<std::uint8_t>(y & 0x0FU);
  for (std::size_t i = 0; i < map.marker_count; ++i) {
    if (map.marker_x[i] == cx && map.marker_y[i] == cy) {
      return map.marker_kind[i];
    }
  }
  return automap_marker::none;
}

std::size_t automap_state::records_used() const noexcept {
  std::size_t used = 0;
  for (const automap_record& map : records_) {
    if (map.used) {
      ++used;
    }
  }
  return used;
}

bool automap_state::appearance_is_for(std::uint8_t area, std::uint8_t geo,
                                      std::uint16_t banks) const noexcept {
  return appearance_valid_ && appearance_area_ == area &&
         appearance_geo_ == geo && appearance_banks_ == banks;
}

void automap_state::begin_appearance(std::uint8_t area, std::uint8_t geo,
                                     std::uint16_t banks) noexcept {
  appearance_valid_ = true;
  appearance_area_ = area;
  appearance_geo_ = geo;
  appearance_banks_ = banks;
  wall_colour_ = {};
  wall_colour_known_ = {};
  door_nibbles_ = 0;
}

bool automap_state::wall_colour_known(unsigned nibble) const noexcept {
  return nibble < wall_colour_known_.size() && wall_colour_known_[nibble];
}

std::uint8_t automap_state::wall_colour(unsigned nibble) const noexcept {
  return nibble < wall_colour_.size() ? wall_colour_[nibble] : 0;
}

void automap_state::set_wall_colour(unsigned nibble,
                                    std::uint8_t colour) noexcept {
  if (nibble >= wall_colour_.size()) {
    return;
  }
  wall_colour_[nibble] = colour;
  wall_colour_known_[nibble] = true;
}

void automap_state::set_panel_covered(bool covered) noexcept {
  if (panel_covered_ == covered) {
    return;
  }
  panel_covered_ = covered;
  // Whichever way it went, the program has just painted on the panel's
  // cells: a clear took them, or the roster came back over them. Either
  // way what this seam put there is gone, and the next arrival has to
  // draw again rather than compare a signature and decide it need not.
  panel_on_screen_ = false;
  drawn_signature_ = 0;
}

bool automap_state::rect_meets_panel(std::uint8_t bottom, std::uint8_t right,
                                     std::uint8_t top,
                                     std::uint8_t left) noexcept {
  return top <= automap_panel_bottom_row && bottom >= automap_panel_top_row &&
         left <= automap_panel_right_col && right >= automap_panel_left_col;
}

bool automap_state::observe(automap_map_kind kind, std::uint8_t disk,
                            std::uint8_t area, std::uint8_t geo, std::uint8_t x,
                            std::uint8_t y) noexcept {
  if (settled_ && (kind != settled_kind_ || disk != settled_disk_ ||
                   area != settled_area_ || geo != settled_geo_)) {
    // The map changed under the party. The cell it *left* is the last one
    // that was ever true, so it is the one worth marking, and everything
    // about the new map has to be waited for.
    automap_record& previous =
        record_for(settled_kind_, settled_disk_, settled_area_, settled_geo_);
    mark(previous, settled_x_, settled_y_, automap_marker::exit);
    unsettle();
    // ...and wherever the party turns out to be standing when it stops
    // moving is where it came in. Only then: the *first* position this
    // ever settles on is wherever the party happened to be when the seam
    // was switched on, which is not an arrival and must not be marked as
    // one.
    pending_entrance_ = true;
  }

  if (settled_) {
    settled_x_ = x;
    settled_y_ = y;
    return true;
  }

  if (looks_ > 0 && x == looking_x_ && y == looking_y_) {
    ++looks_;
  } else {
    looking_x_ = x;
    looking_y_ = y;
    looks_ = 1;
  }
  if (looks_ < settle_looks) {
    return false;
  }

  settled_ = true;
  settled_kind_ = kind;
  settled_disk_ = disk;
  settled_area_ = area;
  settled_geo_ = geo;
  settled_x_ = x;
  settled_y_ = y;
  if (pending_entrance_) {
    mark(record_for(kind, disk, area, geo), x, y, automap_marker::entrance);
    pending_entrance_ = false;
  }
  return true;
}

void automap_state::unsettle() noexcept {
  settled_ = false;
  settled_disk_ = 0;
  settled_area_ = 0;
  settled_geo_ = 0;
  settled_x_ = 0;
  settled_y_ = 0;
  looking_x_ = 0;
  looking_y_ = 0;
  looks_ = 0;
  // A caller that unsettles on its own account — a loaded save teleports
  // the party — has not arrived anywhere; only `observe()`'s own map
  // change is an arrival, and it sets this again after calling here.
  pending_entrance_ = false;
}

// ---------------------------------------------------------------------------
// The sidecar (M5-E2c, #173)
// ---------------------------------------------------------------------------

namespace {

/// Where one record's fields sit inside its fixed-width row, at the
/// version this build writes (automap.h).
constexpr std::size_t field_disk = 0;
constexpr std::size_t field_area = 1;
constexpr std::size_t field_geo = 2;
constexpr std::size_t field_kind = 3;
constexpr std::size_t field_marker_count = 4;
constexpr std::size_t field_seen = 5;
constexpr std::size_t field_marker_x = field_seen + automap_seen_bytes;
constexpr std::size_t field_marker_y = field_marker_x + automap_max_markers;
constexpr std::size_t field_marker_kind = field_marker_y + automap_max_markers;

/// And where they sat in version 1, which had no kind byte and a bitmap
/// of thirty-two. Every record in such a file is a grid.
constexpr std::size_t v1_field_marker_count = 3;
constexpr std::size_t v1_field_seen = 4;
constexpr std::size_t v1_field_marker_x =
    v1_field_seen + automap_grid_seen_bytes;
constexpr std::size_t v1_field_marker_y =
    v1_field_marker_x + automap_max_markers;
constexpr std::size_t v1_field_marker_kind =
    v1_field_marker_y + automap_max_markers;

/// Whether a byte is a map kind this build knows. Same rule as the
/// marker kinds below: an unknown one is not guessed at.
[[nodiscard]] bool known_map_kind(std::uint8_t kind) noexcept {
  return kind == static_cast<std::uint8_t>(automap_map_kind::grid) ||
         kind == static_cast<std::uint8_t>(automap_map_kind::overland);
}

[[nodiscard]] std::uint16_t word_at(std::span<const std::uint8_t> bytes,
                                    std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
}

void put_word(std::span<std::uint8_t> bytes, std::size_t offset,
              std::uint16_t value) noexcept {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

/// Whether a byte is a marker kind this build knows. An unknown one is
/// dropped rather than stored: `automap_marker`'s values are part of the
/// layout, and a renderer switching on one it has never heard of is how a
/// forward-compatible format becomes a crash.
[[nodiscard]] bool known_marker(std::uint8_t kind) noexcept {
  return kind == static_cast<std::uint8_t>(automap_marker::entrance) ||
         kind == static_cast<std::uint8_t>(automap_marker::exit);
}

}  // namespace

void automap_state::forget_records() noexcept {
  records_ = {};
  next_slot_ = 0;
  ++serial_;
  // The map on the screen is not the map in the store any more.
  drawn_signature_ = 0;
}

std::size_t automap_state::sidecar_bytes() const noexcept {
  return automap_sidecar_header_bytes +
         (records_used() * automap_sidecar_record_bytes);
}

std::size_t automap_state::write_sidecar(
    std::span<std::uint8_t> out) const noexcept {
  const std::size_t wanted = sidecar_bytes();
  if (out.size() < wanted) {
    return 0;
  }

  for (std::size_t i = 0; i < automap_sidecar_magic.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(automap_sidecar_magic[i]);
  }
  out[automap_sidecar_magic.size()] = automap_sidecar_version;
  put_word(out, 4, static_cast<std::uint16_t>(records_used()));
  put_word(out, 6, static_cast<std::uint16_t>(automap_sidecar_record_bytes));

  std::size_t at = automap_sidecar_header_bytes;
  for (const automap_record& map : records_) {
    if (!map.used) {
      continue;
    }
    const std::span<std::uint8_t> row =
        out.subspan(at, automap_sidecar_record_bytes);
    row[field_disk] = map.disk;
    row[field_area] = map.area;
    row[field_geo] = map.geo;
    row[field_kind] = static_cast<std::uint8_t>(map.kind);
    row[field_marker_count] = map.marker_count;
    for (std::size_t b = 0; b < automap_seen_bytes; ++b) {
      row[field_seen + b] = map.seen[b];
    }
    for (std::size_t m = 0; m < automap_max_markers; ++m) {
      row[field_marker_x + m] = map.marker_x[m];
      row[field_marker_y + m] = map.marker_y[m];
      row[field_marker_kind + m] =
          static_cast<std::uint8_t>(map.marker_kind[m]);
    }
    at += automap_sidecar_record_bytes;
  }
  return wanted;
}

bool automap_state::read_sidecar(std::span<const std::uint8_t> in) noexcept {
  if (in.size() < automap_sidecar_header_bytes) {
    return false;
  }
  for (std::size_t i = 0; i < automap_sidecar_magic.size(); ++i) {
    if (in[i] != static_cast<std::uint8_t>(automap_sidecar_magic[i])) {
      return false;
    }
  }

  // **Two versions are read and one is written** (automap.h). A player's
  // `AFMAP.DAT` from the build before the overland existed has to open:
  // a version bump that refused it would throw away exactly what the
  // sidecar is for. Anything else is still refused with nothing touched.
  const std::uint8_t version = in[automap_sidecar_magic.size()];
  if (version != automap_sidecar_version &&
      version != automap_sidecar_first_version) {
    return false;
  }
  const bool first = version == automap_sidecar_first_version;

  const std::size_t count = word_at(in, 4);
  const std::size_t stride = word_at(in, 6);
  if (stride != (first ? automap_sidecar_v1_record_bytes
                       : automap_sidecar_record_bytes)) {
    return false;
  }
  if (count > max_records) {
    return false;
  }
  if (in.size() < automap_sidecar_header_bytes + (count * stride)) {
    // Truncated. Nothing is taken from it: half a table read as a whole
    // one is a map with holes in it that nobody can tell from unexplored
    // ground.
    return false;
  }

  records_ = {};
  next_slot_ = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::span<const std::uint8_t> row =
        in.subspan(automap_sidecar_header_bytes + (i * stride), stride);
    const std::size_t at_count =
        first ? v1_field_marker_count : field_marker_count;
    const std::size_t at_seen = first ? v1_field_seen : field_seen;
    const std::size_t at_marker_x = first ? v1_field_marker_x : field_marker_x;
    const std::size_t at_marker_y = first ? v1_field_marker_y : field_marker_y;
    const std::size_t at_marker_kind =
        first ? v1_field_marker_kind : field_marker_kind;
    const std::size_t seen_bytes =
        first ? automap_grid_seen_bytes : automap_seen_bytes;

    automap_record& map = records_[next_slot_++];
    map.used = true;
    // A version-1 record is a grid by construction; a version-2 one whose
    // kind byte is not one this build knows is *dropped to a grid* rather
    // than guessed at, and its bitmap comes across unchanged — the row is
    // still a row, and a reader that invented a kind would address it
    // wrongly.
    map.kind = (!first && known_map_kind(row[field_kind]))
                   ? static_cast<automap_map_kind>(row[field_kind])
                   : automap_map_kind::grid;
    map.disk = row[field_disk];
    map.area = row[field_area];
    map.geo = row[field_geo];
    map.marker_count = row[at_count] <= automap_max_markers
                           ? row[at_count]
                           : static_cast<std::uint8_t>(automap_max_markers);
    for (std::size_t b = 0; b < seen_bytes; ++b) {
      map.seen[b] = row[at_seen + b];
    }
    for (std::size_t m = 0; m < automap_max_markers; ++m) {
      map.marker_x[m] = row[at_marker_x + m];
      map.marker_y[m] = row[at_marker_y + m];
      const std::uint8_t kind = row[at_marker_kind + m];
      map.marker_kind[m] = known_marker(kind)
                               ? static_cast<automap_marker>(kind)
                               : automap_marker::none;
    }
  }
  if (next_slot_ >= max_records) {
    next_slot_ = 0;
  }

  ++serial_;
  // The map on the screen is not the map in the store any more.
  drawn_signature_ = 0;
  return true;
}

}  // namespace amberfolio::machine
