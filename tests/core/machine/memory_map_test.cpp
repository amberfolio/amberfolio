// SPDX-License-Identifier: AGPL-3.0-only
//
// The map, on its own: which of the four answers a physical address gets,
// and what claiming a window does to that. Routing a *cycle* is the
// machine's job and is tested next door; this file is only about the
// decision.

#include "amberfolio/machine/memory_map.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "amberfolio/cpu/address.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_device;

/// On the heap, here and everywhere: a map has the machine's megabyte
/// inside it, which is more automatic storage than a Windows thread has.
std::unique_ptr<memory_map> make(memory_layout layout) {
  return std::make_unique<memory_map>(layout);
}

TEST(memory_map_pc, divides_the_megabyte_the_way_the_pc_does) {
  const auto map = make(memory_layout::pc);

  EXPECT_EQ(map->classify(0x00000), region::ram);
  EXPECT_EQ(map->classify(0x9FFFF), region::ram);

  // The video window, with nothing in the slot. Open bus rather than a
  // fourth kind of region: until a card claims it, it is exactly as
  // absent as the hole above it.
  EXPECT_EQ(map->classify(0xA0000), region::open_bus);
  EXPECT_EQ(map->classify(0xBFFFF), region::open_bus);

  // The hole.
  EXPECT_EQ(map->classify(0xC0000), region::open_bus);
  EXPECT_EQ(map->classify(0xEFFFF), region::open_bus);

  EXPECT_EQ(map->classify(0xF0000), region::rom);
  EXPECT_EQ(map->classify(0xFFFFF), region::rom);
}

TEST(memory_map_flat, is_ram_from_end_to_end) {
  const auto map = make(memory_layout::flat);

  EXPECT_EQ(map->classify(0x00000), region::ram);
  EXPECT_EQ(map->classify(0xA0000), region::ram);
  EXPECT_EQ(map->classify(0xC0000), region::ram);
  EXPECT_EQ(map->classify(0xFFFFF), region::ram);
}

// The CPU folds and wraps before the bus ever sees an address (address.h),
// so this is about a caller with a bug — and the honest answer for an
// address the machine does not have is that nothing answers for it.
TEST(memory_map_both_layouts, has_nothing_above_the_megabyte) {
  EXPECT_EQ(make(memory_layout::pc)->classify(cpu::address_space_size),
            region::open_bus);
  EXPECT_EQ(make(memory_layout::flat)->classify(cpu::address_space_size),
            region::open_bus);
}

TEST(memory_map_claim, routes_the_window_it_was_given) {
  const auto map = make(memory_layout::pc);
  recording_device ega;

  ASSERT_TRUE(map->claim({.first = 0xA0000, .last = 0xAFFFF}, ega));

  EXPECT_EQ(map->classify(0xA0000), region::device);
  EXPECT_EQ(map->owner(0xA0000), &ega);
  EXPECT_EQ(map->classify(0xAFFFF), region::device);

  // What the card did not claim stays absent — which is the case the EGA
  // actually presents (#47): mode 0Dh does not map B0000, so a touch of
  // it is a log line rather than a plane nobody wrote.
  EXPECT_EQ(map->classify(0xB0000), region::open_bus);
  EXPECT_EQ(map->owner(0xB0000), nullptr);
}

TEST(memory_map_claim, wins_over_the_layout) {
  const auto map = make(memory_layout::flat);
  recording_device card;

  // Every address is RAM under this layout, so a claim that did not take
  // precedence could not be honoured at all — and shadowing address space
  // that would otherwise be memory is what a card on this bus does.
  ASSERT_TRUE(map->claim({.first = 0x10000, .last = 0x1FFFF}, card));

  EXPECT_EQ(map->classify(0x0FFFF), region::ram);
  EXPECT_EQ(map->classify(0x10000), region::device);
  EXPECT_EQ(map->classify(0x20000), region::ram);
}

TEST(memory_map_claim, refuses_to_overlap_a_window_already_taken) {
  const auto map = make(memory_layout::pc);
  recording_device first;
  recording_device second;

  ASSERT_TRUE(map->claim({.first = 0xA0000, .last = 0xAFFFF}, first));

  EXPECT_FALSE(map->claim({.first = 0xAFFFF, .last = 0xBFFFF}, second));
  EXPECT_EQ(map->owner(0xAFFFF), &first);
  EXPECT_EQ(map->classify(0xB0000), region::open_bus);

  // Refused, not partially applied: the range that did not overlap is not
  // routed either.
  EXPECT_EQ(map->owner(0xB0000), nullptr);
}

TEST(memory_map_claim, refuses_a_window_that_ends_before_it_begins) {
  const auto map = make(memory_layout::pc);
  recording_device card;

  EXPECT_FALSE(map->claim({.first = 0xB0000, .last = 0xAFFFF}, card));
}

TEST(memory_map_claim, refuses_more_windows_than_it_has_room_for) {
  const auto map = make(memory_layout::pc);
  recording_device card;

  for (std::size_t i = 0; i < memory_map::max_windows; ++i) {
    const auto base = static_cast<std::uint32_t>(0xC0000 + (i * 0x1000));
    EXPECT_TRUE(map->claim({.first = base, .last = base + 0xFFF}, card));
  }

  EXPECT_FALSE(map->claim({.first = 0xE0000, .last = 0xE0FFF}, card));
  EXPECT_EQ(map->classify(0xE0000), region::open_bus);
}

TEST(memory_map_ram, is_the_whole_megabyte_and_starts_at_zero) {
  const auto map = make(memory_layout::pc);

  ASSERT_EQ(map->ram().size(), cpu::address_space_size);
  EXPECT_EQ(map->ram()[0], 0);
  EXPECT_EQ(map->ram()[cpu::address_space_size - 1], 0);

  // The back door reaches the parts of the map a bus cycle cannot: the
  // hole, and the ROM the machine itself has to write (memory_map.h).
  map->ram()[0xF0000] = 0xCF;
  EXPECT_EQ(map->ram()[0xF0000], 0xCF);
}

}  // namespace
}  // namespace amberfolio::machine
