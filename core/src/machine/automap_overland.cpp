// SPDX-License-Identifier: AGPL-3.0-only
//
// The overland recorder (automap_overland.h): the guard, then the one bit
// it sets.

#include "automap_overland.h"

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {
namespace {

/// The program this file's facts are about (program_profile.h): the
/// baseline edition, and only it.
constexpr const program_profile& profile = pool_of_radiance;

// ---------------------------------------------------------------------------
// The facts (docs/explored-overlay.md §2)
// ---------------------------------------------------------------------------

/// **Which disk and area each view kind is**, from the automap's zone
/// table: kind 2 is area 25 on disk 6, kind 3 area 26 on disk 7, kind 4
/// area 27 on disk 8. Crossing a band edge, the program sets the new disk
/// and area a moment before the new kind (driven: disk 7 and area 26 with
/// the kind still 2), and a look then would credit the old band's cell to
/// the new band's record.
constexpr std::uint8_t first_overland_disk = 6;
constexpr std::uint8_t first_overland_area = 25;

/// How far into the record this reads, which is what the bounds check
/// below is about.
constexpr std::uint16_t record_reach = profile.area_shown_in_3d + 2;

/// Where the bar is in the input routine's frame. It takes eleven words
/// and cleans twenty-two bytes; its callers push them deepest-first, so
/// at the thunk's entry — the far return address on top and nothing else
/// — the menu's far pointer is the fourth argument from the far end.
constexpr std::uint16_t bar_frame_menu_offset = 18;
constexpr std::uint16_t bar_frame_menu_segment = 20;

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
  if (cpu.read_byte(ds, profile.data_game_mode) != profile.mode_travel) {
    return found;
  }
  const std::uint8_t kind = cpu.read_byte(ds, profile.data_view_kind);
  if (kind < profile.view_kind_first_overland ||
      kind > profile.view_kind_last_overland) {
    return found;
  }
  if (cpu.read_byte(ds, profile.data_in_transition) != 0) {
    return found;
  }

  const std::uint16_t record_offset =
      cpu.read_word(ds, profile.data_area_record);
  const std::uint16_t record_segment =
      cpu.read_word(ds, at(profile.data_area_record, 2));
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
  if (cpu.read_word(record_segment,
                    at(record_offset, profile.area_shown_in_3d)) != 0) {
    return found;
  }

  const std::uint16_t column = cpu.read_word(
      record_segment, at(record_offset, profile.area_overland_column));
  const std::uint16_t row = cpu.read_word(
      record_segment, at(record_offset, profile.area_overland_row));
  if (column >= automap_overland_columns || row >= automap_overland_rows) {
    // The program clamps both in its own move step, so a position outside
    // them means this record is not the record these offsets are facts
    // about. Decline and touch nothing (docs/seams.md §2).
    ctx.decline(seam_reason::point_not_recognized);
    return found;
  }

  const std::uint8_t disk = cpu.read_byte(ds, profile.data_disk_number);
  const std::uint8_t area = cpu.read_byte(ds, profile.data_area_id);
  const auto band =
      static_cast<std::uint8_t>(kind - profile.view_kind_first_overland);
  if (disk != first_overland_disk + band ||
      area != first_overland_area + band) {
    // Halfway across a band edge: the three bytes do not yet name one
    // wilderness area, so the position belongs to neither. Not a decline;
    // the program passes through this on every crossing.
    return found;
  }

  found.on_screen = true;
  found.view_kind = kind;
  found.disk = disk;
  found.area = area;
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

void note_command_bar(machine& box, const seam_context& ctx, std::uint16_t ds) {
  cpu::processor& cpu = box.processor();
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const std::uint16_t segment =
      cpu.read_word(ss, at(sp, bar_frame_menu_segment));
  const std::uint16_t offset = cpu.read_word(ss, at(sp, bar_frame_menu_offset));
  const bool program_bar = segment == ds && (offset == profile.data_area_bar ||
                                             offset == profile.data_view_bar);

  // The adventuring loop's two calls, by the far return on top of the
  // stack: `modern-controls`' move mode hands them a copy of the bar on
  // the stack (#479), so the call site says whose bar it is when the
  // pointer no longer can.
  const std::uint16_t return_ip = cpu.read_word(ss, sp);
  const std::uint16_t return_cs = cpu.read_word(ss, at(sp, 2));
  const std::uint16_t adventure = cpu.read_word(
      static_cast<std::uint16_t>(ctx.image_base() >> 4U),
      static_cast<std::uint16_t>(profile.adventure_overlay.load_segment_at));
  const bool party_call = adventure != 0 && return_cs == adventure &&
                          (return_ip == profile.area_bar_return ||
                           return_ip == profile.view_bar_return);

  box.automap().set_at_command_bar(program_bar || party_call);
}

}  // namespace amberfolio::machine
