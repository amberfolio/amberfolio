// SPDX-License-Identifier: AGPL-3.0-only
//
// The overland recorder (automap_overland.h): the guard, then the one bit
// it sets.

#include "automap_overland.h"

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts (docs/explored-overlay.md §2)
// ---------------------------------------------------------------------------

/// The game mode, and the value that is the wilderness travel view.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_overland = 3;

/// Which view is up. 2, 3 and 4 are the three wilderness areas; 1 is the
/// interior grid the automap panel is a map of.
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint8_t view_kind_first_overland = 2;
constexpr std::uint8_t view_kind_last_overland = 4;

/// Non-zero while the program is running a scripted move between areas.
constexpr std::uint16_t data_in_transition = 0x442F;

/// The disk the area's files come from, and the area's own id.
constexpr std::uint16_t data_disk_number = 0x5376;
constexpr std::uint16_t data_area_id = 0x84DC;

/// The current area record: a far pointer, offset then segment.
constexpr std::uint16_t data_area_record = 0x49D2;

/// Inside that record: the party's overland column and row, each a word,
/// and the word that says the program is showing these areas in the
/// interior view instead.
constexpr std::uint16_t record_overland_column = 0x186;
constexpr std::uint16_t record_overland_row = 0x188;
constexpr std::uint16_t record_shown_in_3d = 0x1CC;

/// How far into the record this reads, which is what the bounds check
/// below is about.
constexpr std::uint16_t record_reach = record_shown_in_3d + 2;

[[nodiscard]] std::uint16_t at(std::uint16_t base, std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

}  // namespace

overland_look observe_overland(machine& box, seam_context& ctx,
                               std::uint16_t ds) {
  overland_look found{};
  cpu::processor& cpu = box.processor();

  // The cheap data-segment bytes first, and no pointer followed until
  // they hold (docs/seams.md §8.4's wild-read rule).
  if (cpu.read_byte(ds, data_game_mode) != mode_overland) {
    return found;
  }
  const std::uint8_t kind = cpu.read_byte(ds, data_view_kind);
  if (kind < view_kind_first_overland || kind > view_kind_last_overland) {
    return found;
  }
  if (cpu.read_byte(ds, data_in_transition) != 0) {
    return found;
  }

  const std::uint16_t record_offset = cpu.read_word(ds, data_area_record);
  const std::uint16_t record_segment =
      cpu.read_word(ds, at(data_area_record, 2));
  if (record_segment == 0) {
    return found;
  }
  const std::uint32_t base =
      cpu::physical_address(record_segment, record_offset);
  if (record_offset > static_cast<std::uint16_t>(0x10000U - record_reach) ||
      base + record_reach > conventional_ram_size) {
    // A far pointer that has not been set up yet points anywhere, and a
    // read above conventional memory is a read of the video window, where
    // it loads the adapter's latches. Refuse rather than look.
    return found;
  }

  // **The guard a mode-and-kind test alone would miss.** With this word
  // non-zero the program shows these very areas in the interior view,
  // with the view-kind byte still reading 2, 3 or 4 — so a seam that
  // trusted the kind byte would be recording, and drawing, against a
  // screen that is not the travel view at all.
  if (cpu.read_word(record_segment, at(record_offset, record_shown_in_3d)) !=
      0) {
    return found;
  }

  const std::uint16_t column =
      cpu.read_word(record_segment, at(record_offset, record_overland_column));
  const std::uint16_t row =
      cpu.read_word(record_segment, at(record_offset, record_overland_row));
  if (column >= automap_overland_columns || row >= automap_overland_rows) {
    // The program clamps both in its own move step, so a position outside
    // them means this record is not the record these offsets are facts
    // about. Decline and touch nothing (docs/seams.md §2).
    ctx.decline(seam_reason::point_not_recognized);
    return found;
  }

  found.on_screen = true;
  found.view_kind = kind;
  found.disk = cpu.read_byte(ds, data_disk_number);
  found.area = cpu.read_byte(ds, data_area_id);
  found.x = static_cast<std::uint8_t>(column);
  found.y = static_cast<std::uint8_t>(row);

  automap_state& state = box.automap();
  found.settled =
      state.observe_overland(found.disk, found.area, found.x, found.y);
  if (!found.settled) {
    // The position is not to be believed yet: the program's own arrival
    // has not placed the party. Nothing is revealed from it.
    return found;
  }

  const std::uint32_t was = state.serial();
  (void)state.reveal(state.record_for_overland(found.disk, found.area), found.x,
                     found.y);
  if (state.serial() != was) {
    // Something is explored that was not, so a host that is storing the
    // table beside the save is told which version it is now (M5-E2c).
    // Nothing is done with the answer: a host that has attached nothing is
    // the ordinary case and not a failure.
    (void)ctx.call_host(seam_host_service::automap_update, state.serial());
  }
  return found;
}

}  // namespace amberfolio::machine
