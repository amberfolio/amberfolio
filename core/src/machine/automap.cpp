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

namespace amberfolio::machine {
namespace {

/// The bit index of a cell. Coordinates wrap at four bits, which is what
/// the program's own map addressing does.
[[nodiscard]] constexpr unsigned cell_index(unsigned x, unsigned y) noexcept {
  return ((y & 0x0FU) * automap_map_side) + (x & 0x0FU);
}

}  // namespace

void automap_state::clear() noexcept {
  records_ = {};
  next_slot_ = 0;
  serial_ = 0;
  panel_open_ = false;
  panel_on_screen_ = false;
  panel_covered_ = false;
  unsettle();
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
}

automap_record& automap_state::record_for(std::uint8_t disk, std::uint8_t area,
                                          std::uint8_t geo) noexcept {
  for (automap_record& map : records_) {
    if (map.used && map.disk == disk && map.area == area && map.geo == geo) {
      return map;
    }
  }
  for (automap_record& map : records_) {
    if (!map.used) {
      map = {};
      map.used = true;
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
  map.disk = disk;
  map.area = area;
  map.geo = geo;
  ++serial_;
  return map;
}

const automap_record* automap_state::find(std::uint8_t disk, std::uint8_t area,
                                          std::uint8_t geo) const noexcept {
  for (const automap_record& map : records_) {
    if (map.used && map.disk == disk && map.area == area && map.geo == geo) {
      return &map;
    }
  }
  return nullptr;
}

bool automap_state::seen(const automap_record& map, unsigned x,
                         unsigned y) noexcept {
  const unsigned index = cell_index(x, y);
  return ((map.seen[index / 8] >> (index % 8)) & 1U) != 0;
}

bool automap_state::reveal(automap_record& map, unsigned x,
                           unsigned y) noexcept {
  const unsigned index = cell_index(x, y);
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

bool automap_state::observe(std::uint8_t disk, std::uint8_t area,
                            std::uint8_t geo, std::uint8_t x,
                            std::uint8_t y) noexcept {
  if (settled_ &&
      (disk != settled_disk_ || area != settled_area_ || geo != settled_geo_)) {
    // The map changed under the party. The cell it *left* is the last one
    // that was ever true, so it is the one worth marking, and everything
    // about the new map has to be waited for.
    automap_record& previous =
        record_for(settled_disk_, settled_area_, settled_geo_);
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
  settled_disk_ = disk;
  settled_area_ = area;
  settled_geo_ = geo;
  settled_x_ = x;
  settled_y_ = y;
  if (pending_entrance_) {
    mark(record_for(disk, area, geo), x, y, automap_marker::entrance);
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

}  // namespace amberfolio::machine
