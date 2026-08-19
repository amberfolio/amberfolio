// SPDX-License-Identifier: AGPL-3.0-only
//
// The megabyte, and what answers for each part of it.
//
// PLAN.md §3 gives the map: a 1 MiB real-mode address space with 640 KiB
// of conventional RAM at the bottom. The rest of it is the PC's — a
// window at A0000 the video card answers for, a hole above that, and the
// BIOS region at the top, which M2-F3 (#44) fills with the interrupt
// vector stubs and which this issue only reserves and backs.
//
// Everything the map does not name, and every part of the window no
// device has claimed, is open bus: reads float high, writes go nowhere,
// and the machine says so once (diagnostics.h). That is not a guess about
// what the program meant — it is what the hardware does — and it is the
// difference between "log, don't fake" as a slogan and as a mechanism.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/machine/device.h"

namespace amberfolio::machine {

/// Conventional RAM: 640 KiB, 00000-9FFFF.
inline constexpr std::uint32_t conventional_ram_size = 0xA0000;

/// The video window, A0000-BFFFF. Reserved by the map rather than backed
/// by it: whichever device claims it answers for it. The EGA claims the
/// bottom half in M2-D2 (#47) and deliberately leaves B0000-BFFFF alone,
/// because mode 0Dh does not map it — so the unclaimed remainder stays
/// open bus and a touch of it is a log line, which is exactly the
/// "logging stub" this region wants and is one class less than writing
/// one.
inline constexpr memory_window video_window{.first = 0xA0000, .last = 0xBFFFF};

/// The BIOS region, F0000-FFFFF, and the reason `region::rom` exists.
inline constexpr memory_window bios_region{.first = 0xF0000, .last = 0xFFFFF};

/// What answers for a physical address.
enum class region : std::uint8_t {
  /// Read/write memory.
  ram,
  /// Read-only memory: the BIOS region. Reads come out of the same
  /// backing store RAM does — the machine puts the vector stubs there
  /// through memory_map::ram(), which is not a bus cycle — and writes are
  /// dropped and reported. A machine that quietly accepted them would be
  /// faking RAM at F000, and a program writing there is worth knowing
  /// about rather than worth accommodating.
  rom,
  /// A window some device claimed.
  device,
  /// Nothing answers.
  open_bus,
};

/// Which map the machine is wired for.
enum class memory_layout : std::uint8_t {
  /// The PC map above: RAM, video window, hole, BIOS region.
  pc,
  /// One megabyte of RAM and nothing else — no window, no hole, no ROM.
  ///
  /// Not a machine that ever existed. It is the map the M1 program
  /// harness (tests/programs) runs against, kept as a layout so the same
  /// self-written programs can be run through the machine and compared
  /// against it step for step. That comparison is this issue's exit
  /// criterion: it says the machine's bus is transparent to the CPU.
  flat,
};

/// The physical address space and the routing decisions over it.
///
/// A megabyte lives inside this object, so it is not a thing to put on a
/// stack — see machine.h, which owns one.
class memory_map {
 public:
  /// How many device windows the map has room for. One in M2 (the EGA);
  /// four so that a second video device or a ROM image in the hole does
  /// not need this file reopened, and small enough that `owner()` is a
  /// scan nobody has to think about.
  static constexpr std::size_t max_windows = 4;

  explicit memory_map(memory_layout map = memory_layout::pc) noexcept;

  [[nodiscard]] memory_layout layout() const noexcept { return layout_; }

  /// Route `window` to `dev`. False, and nothing registered, if it
  /// overlaps a window already claimed or there is no room left.
  ///
  /// A claim wins over the layout: the window is what answers, whatever
  /// the layout would otherwise have put there. That is what the video
  /// window is on real hardware — a card shadowing address space that
  /// would otherwise be nothing — and it is what lets a device be
  /// attached to a `flat` machine at all.
  bool claim(memory_window window, device& dev);

  [[nodiscard]] region classify(std::uint32_t address) const noexcept;

  /// The device that claimed `address`, or null if none did. Answers for
  /// exactly the addresses `classify` calls `region::device`.
  [[nodiscard]] device* owner(std::uint32_t address) const noexcept;

  /// The bytes behind the map: the whole megabyte, indexed by physical
  /// address, whether or not the layout maps a given one.
  ///
  /// This is the back door, and it is meant to be one. A loader putting a
  /// program image down (M2-D6), M2-F3 writing the vector table and its
  /// stubs into the BIOS region, and a test arranging a starting state
  /// are all the *machine* writing memory rather than the program: none
  /// of them should be routed to a device, refused by a ROM, or reported
  /// as a touch of nothing.
  ///
  /// It is a full megabyte even under the `pc` layout, where 320 KiB of
  /// it is never read. Indexing by physical address with no arithmetic in
  /// the way is worth more than the memory, and the `flat` layout needs
  /// every byte of it anyway.
  [[nodiscard]] std::span<std::uint8_t> ram() noexcept { return storage_; }
  [[nodiscard]] std::span<const std::uint8_t> ram() const noexcept {
    return storage_;
  }

 private:
  memory_layout layout_;
  std::array<memory_window, max_windows> windows_{};
  std::array<device*, max_windows> owners_{};
  std::size_t claimed_{};
  std::array<std::uint8_t, cpu::address_space_size> storage_{};
};

}  // namespace amberfolio::machine
