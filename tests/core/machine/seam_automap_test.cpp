// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap panel (seam_automap.cpp, M5-E2 #173), exercised through its
// mechanism and not through any program.
//
// Two halves, and the split is deliberate. `automap_state` (automap.h)
// reads nothing and can be tested against nothing at all; the seam is
// tested by laying a data segment and a map out the way the facts say the
// program lays them out, pointing the machine at an interception point,
// and watching what the handler does — to the store, to the panel's
// pixels, and to the planes.
//
// The offsets below are restated rather than read out of the seam, which
// is `seam_cheats_test.cpp`'s rule and the reason for it: a test that took
// its layout from the code it is checking would be agreeing with itself.
// The interception addresses *are* read from the definition, because those
// are the mechanism rather than the layout. Every byte here is this file's
// own (PLAN.md §6).

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

// --- The facts this test lays memory out by --------------------------------

constexpr std::uint32_t dgroup_offset = 0xC7C0;

constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint16_t data_in_transition = 0x442F;
constexpr std::uint16_t data_area_record = 0x49D2;
constexpr std::uint16_t record_geometry_block = 0x18A;
constexpr std::uint16_t data_disk_number = 0x5376;
constexpr std::uint16_t data_area_id = 0x84DC;
constexpr std::uint16_t data_party_x = 0x6AAD;
constexpr std::uint16_t data_party_y = 0x6AAE;
constexpr std::uint16_t data_party_facing = 0x6AAF;
constexpr std::uint16_t data_map_pointer = 0x6A5C;
constexpr std::uint16_t data_frame_colour = 0x6A60;
constexpr std::uint16_t data_ground_colour = 0x6A63;
constexpr std::uint16_t data_key_pushback = 0x8501;
constexpr std::uint16_t data_shape_tiles = 0x6A58;
constexpr std::uint16_t data_shape_first_slot = 0x0C8C;
constexpr std::uint16_t data_shape_columns = 0x0C96;
constexpr std::uint16_t data_shape_rows = 0x0CA0;
constexpr std::uint16_t data_tile_banks = 0x5E3A;
constexpr std::uint16_t data_bank_first_tile = 0x2722;
constexpr std::uint16_t data_wall_set = 0x6AAE;
constexpr std::uint16_t wall_set_stride = 4;
constexpr std::uint16_t shape_row_bytes = 0x9C;
constexpr std::uint16_t tile_header_height = 0x00;
constexpr std::uint16_t tile_header_width = 0x02;
constexpr std::uint16_t tile_header_frame_bytes = 0x11;
constexpr std::uint16_t tile_first_frame = 0x17;

constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint8_t view_kind_area = 1;

/// Where the test puts the things the data segment points at. Segments of
/// their own, so a handler that used the wrong one gives a wrong answer
/// rather than a lucky one.
constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t map_segment = 0x5000;
constexpr std::uint16_t map_shape_segment = 0x6000;
constexpr std::uint16_t map_bank_segment = 0x7000;

constexpr std::uint16_t key_tab = 0x0F09;

/// The map's own four planes, and the lane numbering.
constexpr std::uint16_t plane_faces_ns = 0x000;
constexpr std::uint16_t plane_faces_sw = 0x100;
constexpr std::uint16_t plane_styles = 0x300;
constexpr unsigned lane_north = 0;
constexpr unsigned lane_east = 2;
constexpr unsigned lane_south = 4;
constexpr unsigned lane_west = 6;

/// A five-byte program that does nothing but let steps happen:
/// `MOV AX,1111h ; NOP ; HLT`. The seam's points are reached because the
/// test puts CS:IP on them, not because this program goes there.
constexpr std::array<std::uint8_t, 5> idle_program{0xB8, 0x11, 0x11, 0x90,
                                                   0xF4};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }
  [[nodiscard]] automap_state& map_state() const noexcept {
    return box->automap();
  }

  /// The program's data segment, where the seam insists on finding it: at
  /// a fixed offset in the image the loader placed. A handler that read
  /// whatever DS happened to hold would pass a test that set DS to
  /// anything, so this is the one segment the tests may use.
  [[nodiscard]] static std::uint16_t dgroup() noexcept {
    return static_cast<std::uint16_t>(image_load_segment +
                                      (dgroup_offset / 16));
  }

  void enable() const {
    ASSERT_EQ(box->seams().enable("automap"), seam_reason::none);
  }

  /// The physical address one of the automap's points is armed at, by
  /// index into the definition — read from the definition, because that
  /// is the mechanism under test.
  [[nodiscard]] std::uint32_t point(std::size_t which) const {
    const seam_definition* found = box->seams().find("automap");
    EXPECT_NE(found, nullptr);
    EXPECT_LT(which, found->points.size());
    return cpu::physical_address(image_load_segment, 0) +
           found->points[which].offset;
  }

  [[nodiscard]] std::uint8_t byte_at(std::uint16_t segment,
                                     std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }
  [[nodiscard]] std::uint16_t word_at(std::uint16_t segment,
                                      std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        byte_at(segment, offset) |
        (byte_at(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
  }
  void put_byte(std::uint16_t segment, std::uint16_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = value;
  }
  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(value));
    put_byte(segment, static_cast<std::uint16_t>(offset + 1),
             static_cast<std::uint8_t>(value >> 8U));
  }

  /// Put a HLT at a point's address and stand the processor on it, with a
  /// stack and the program's own data segment. Stepping then runs the
  /// handler and, if it returns, the HLT.
  void stand_on(std::uint32_t address, std::uint16_t sp = 0x0400) const {
    box->memory().ram()[address] = 0xF4;
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = static_cast<std::uint16_t>(
        address - cpu::physical_address(image_load_segment, 0));
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = sp;
  }

  /// The adventuring screen, as far as the seam's guards can see it: the
  /// mode, the view kind, no transition in flight, an area record with a
  /// geometry block in it, and a map.
  void adventuring(std::uint8_t x, std::uint8_t y, std::uint8_t facing,
                   std::uint8_t geo = 3) const {
    const std::uint16_t ds = dgroup();
    put_byte(ds, data_game_mode, mode_adventure);
    put_byte(ds, data_view_kind, view_kind_area);
    put_byte(ds, data_in_transition, 0);
    put_word(ds, data_area_record, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_area_record + 2),
             record_segment);
    put_byte(record_segment, record_geometry_block, geo);
    put_byte(ds, data_disk_number, 3);
    put_byte(ds, data_area_id, 0);
    put_byte(ds, data_party_x, x);
    put_byte(ds, data_party_y, y);
    put_byte(ds, data_party_facing, facing);
    put_word(ds, data_map_pointer, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_map_pointer + 2), map_segment);
    put_byte(ds, data_frame_colour, 6);
    put_byte(ds, data_ground_colour, 2);
    put_byte(ds, data_key_pushback, 0);
  }

  /// A wall face on one lane of one cell, with a style: 0 solid, 1 a way
  /// through. The face nibble is what says there is a face at all.
  void face(unsigned x, unsigned y, unsigned lane, std::uint8_t nibble,
            std::uint8_t style) const {
    const auto cell = static_cast<std::uint16_t>((y * 16) + x);
    const std::uint16_t plane = (lane == lane_north || lane == lane_east)
                                    ? plane_faces_ns
                                    : plane_faces_sw;
    const bool high = lane == lane_north || lane == lane_south;
    const auto at = static_cast<std::uint16_t>(plane + cell);
    const std::uint8_t was = byte_at(map_segment, at);
    put_byte(map_segment, at,
             high ? static_cast<std::uint8_t>((was & 0x0FU) | (nibble << 4U))
                  : static_cast<std::uint8_t>((was & 0xF0U) | nibble));

    const auto style_at = static_cast<std::uint16_t>(plane_styles + cell);
    const std::uint8_t bits = byte_at(map_segment, style_at);
    const auto shift = static_cast<unsigned>(lane & 6U);
    put_byte(map_segment, style_at,
             static_cast<std::uint8_t>((bits & ~(0x03U << shift)) |
                                       (style << shift)));
  }

  /// Both sides of a border, which is what a wall a player can neither
  /// see nor walk through actually is.
  void wall_between(unsigned x, unsigned y, unsigned lane) const {
    static constexpr std::array<int, 8> dx{0, 0, 1, 0, 0, 0, -1, 0};
    static constexpr std::array<int, 8> dy{-1, 0, 0, 0, 1, 0, 0, 0};
    face(x, y, lane, 1, 0);
    const auto nx = static_cast<unsigned>(static_cast<int>(x) + dx[lane]);
    const auto ny = static_cast<unsigned>(static_cast<int>(y) + dy[lane]);
    if (nx < automap_map_side && ny < automap_map_side) {
      face(nx, ny, (lane + 4) % 8, 1, 0);
    }
  }

  /// The tiles the 3D renderer blits for a wall face of kind `nibble`,
  /// as far as the panel's colour histogram is concerned: one shape, one
  /// tile, one byte column of one scanline, every pixel of it `colour`.
  ///
  /// Laid out from the facts rather than by calling the seam's own
  /// reader — the shape-tile table, the shape geometry, the bank's first
  /// code and the tile-set header are what the program holds, and a test
  /// that took them from the code under test would be agreeing with
  /// itself.
  void wall_texture(std::uint8_t nibble, std::uint8_t colour) const {
    const std::uint16_t ds = dgroup();
    put_word(ds, data_shape_tiles, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_shape_tiles + 2),
             map_shape_segment);
    // One shape, covering one tile, starting at slot 0.
    put_byte(ds, data_shape_first_slot, 0);
    put_byte(ds, data_shape_columns, 1);
    put_byte(ds, data_shape_rows, 1);
    put_byte(map_shape_segment,
             static_cast<std::uint16_t>((nibble - 1) * shape_row_bytes), 1);

    put_word(ds, data_bank_first_tile, 1);
    put_word(ds, data_tile_banks, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_tile_banks + 2),
             map_bank_segment);
    put_word(map_bank_segment, tile_header_height, 1);
    put_word(map_bank_segment, tile_header_width, 1);
    put_word(map_bank_segment, tile_header_frame_bytes, 4);
    for (unsigned plane = 0; plane < 4; ++plane) {
      put_byte(map_bank_segment,
               static_cast<std::uint16_t>(tile_first_frame + plane),
               ((colour >> plane) & 1U) != 0 ? 0xFF : 0x00);
    }
  }

  /// A key in the BIOS keystroke buffer, the way a typed one arrives.
  void type(std::uint16_t keystroke) const {
    const std::uint16_t tail = word_at(bda::segment, bda::keyboard_buffer_tail);
    put_word(bda::segment, tail, keystroke);
    auto next = static_cast<std::uint16_t>(tail + 2U);
    if (next >= bda::keyboard_buffer_end) {
      next = bda::keyboard_buffer;
    }
    put_word(bda::segment, bda::keyboard_buffer_tail, next);
  }

  [[nodiscard]] std::size_t keys_waiting() const {
    const std::uint16_t head = word_at(bda::segment, bda::keyboard_buffer_head);
    const std::uint16_t tail = word_at(bda::segment, bda::keyboard_buffer_tail);
    const auto span = static_cast<std::uint16_t>(bda::keyboard_buffer_end -
                                                 bda::keyboard_buffer);
    return static_cast<std::size_t>((tail + span - head) % span) / 2U;
  }

  /// Step the keyboard-poll point `times` times. Three is what it takes
  /// for a position to be believed; a fourth is the first pass that can
  /// draw anything.
  void poll(unsigned times) const {
    for (unsigned i = 0; i < times; ++i) {
      stand_on(point(0));
      box->step();
    }
  }

  /// One pixel of the panel, as it stands in the planes.
  [[nodiscard]] std::uint8_t screen_pixel(unsigned x, unsigned y) const {
    const auto offset = static_cast<std::uint16_t>((y * 40U) + (x / 8U));
    const unsigned shift = 7U - (x % 8U);
    std::uint8_t colour = 0;
    for (unsigned plane = 0; plane < ega::plane_count; ++plane) {
      const std::uint8_t bits =
          video->plane_byte(static_cast<unsigned>(plane), offset);
      colour =
          static_cast<std::uint8_t>(colour | (((bits >> shift) & 1U) << plane));
    }
    return colour;
  }

  /// The EGA, attached so there is something for a plane write to reach.
  void attach_video() {
    video = std::make_unique<ega>(*box);
    box->attach(*video);
  }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
};

// ---------------------------------------------------------------------------
// The store, on its own
// ---------------------------------------------------------------------------

TEST(AutomapState, RecordsAreKeyedOnAllThreeBytesOfAMapIdentity) {
  automap_state state;
  automap_record& first = state.record_for(3, 0, 1);
  state.reveal(first, 4, 4);

  // Same disk and area, a different geometry block: a different map, and
  // it must not inherit the first one's explored cells. This is the trap
  // automap.h names — a castle quadrant swapped in place keeps its area.
  const automap_record& swapped = state.record_for(3, 0, 2);
  EXPECT_FALSE(automap_state::seen(swapped, 4, 4));
  EXPECT_TRUE(automap_state::seen(*state.find(3, 0, 1), 4, 4));
  EXPECT_EQ(state.records_used(), 2u);
  EXPECT_EQ(state.find(3, 0, 9), nullptr);
}

TEST(AutomapState, RevealingIsIdempotentAndOnlyChangeMovesTheSerial) {
  automap_state state;
  automap_record& map = state.record_for(1, 1, 1);
  const std::uint32_t before = state.serial();
  EXPECT_TRUE(state.reveal(map, 2, 3));
  EXPECT_NE(state.serial(), before);

  const std::uint32_t after = state.serial();
  EXPECT_FALSE(state.reveal(map, 2, 3));
  EXPECT_EQ(state.serial(), after) << "nothing changed, so nothing moved";
  EXPECT_TRUE(automap_state::seen(map, 2, 3));
  EXPECT_TRUE(automap_state::seen(map, 2 + 16, 3 + 16))
      << "coordinates wrap at four bits, as the program's own do";
}

TEST(AutomapState, MarksDedupeByPositionAndTheFirstKindWins) {
  automap_state state;
  automap_record& map = state.record_for(1, 1, 1);
  state.mark(map, 5, 5, automap_marker::entrance);
  state.mark(map, 5, 5, automap_marker::exit);
  EXPECT_EQ(map.marker_count, 1);
  EXPECT_EQ(automap_state::marker_at(map, 5, 5), automap_marker::entrance);
  EXPECT_EQ(automap_state::marker_at(map, 5, 6), automap_marker::none);

  for (unsigned i = 0; i < automap_max_markers + 4; ++i) {
    state.mark(map, i, 0, automap_marker::exit);
  }
  EXPECT_EQ(map.marker_count, automap_max_markers)
      << "a bound, not a growth policy";
}

TEST(AutomapState, APositionIsNotBelievedUntilItHoldsStill) {
  automap_state state;
  EXPECT_FALSE(state.observe(3, 0, 1, 7, 5));
  EXPECT_FALSE(state.observe(3, 0, 1, 7, 5));
  EXPECT_TRUE(state.observe(3, 0, 1, 7, 5));
  EXPECT_TRUE(state.settled());
  EXPECT_EQ(state.settled_x(), 7);

  // Nothing was marked: the first position this ever settles on is
  // wherever the party happened to be, not an arrival.
  EXPECT_EQ(state.find(3, 0, 1), nullptr);
}

TEST(AutomapState, AMapChangeMarksTheCellTheePartyLeftAndTheOneItArrivesAt) {
  automap_state state;
  for (int i = 0; i < 3; ++i) {
    state.observe(3, 0, 1, 7, 5);
  }
  ASSERT_TRUE(state.settled());
  state.observe(3, 0, 1, 0, 4);  // walked to the gate

  // A new map, and the position words still hold the old cell for a
  // moment. Nothing is believed until it stops moving.
  EXPECT_FALSE(state.observe(3, 8, 1, 0, 4));
  EXPECT_FALSE(state.observe(3, 8, 1, 15, 4));
  EXPECT_FALSE(state.observe(3, 8, 1, 15, 4));
  EXPECT_TRUE(state.observe(3, 8, 1, 15, 4));

  const automap_record* left = state.find(3, 0, 1);
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(automap_state::marker_at(*left, 0, 4), automap_marker::exit);
  const automap_record* arrived = state.find(3, 8, 1);
  ASSERT_NE(arrived, nullptr);
  EXPECT_EQ(automap_state::marker_at(*arrived, 15, 4),
            automap_marker::entrance);
}

TEST(AutomapState, TheStatusRowIsNotThePanels) {
  // The program clears its status row as the clock ticks. A panel that
  // called that "something took my cells" would flicker once a minute.
  EXPECT_FALSE(automap_state::rect_meets_panel(0x0F, 0x26, 0x0F, 0x11));
  EXPECT_TRUE(automap_state::rect_meets_panel(0x0E, 0x26, 0x0E, 0x11));
  EXPECT_TRUE(automap_state::rect_meets_panel(0x18, 0x27, 0x00, 0x00));
  EXPECT_FALSE(automap_state::rect_meets_panel(0x18, 0x10, 0x00, 0x00))
      << "the left-hand viewport is not the panel's";
}

TEST(AutomapState, ClearingDropsEverything) {
  automap_state state;
  automap_record& map = state.record_for(1, 2, 3);
  state.reveal(map, 1, 1);
  state.set_panel_open(true);
  state.set_panel_on_screen(true);
  state.clear();
  EXPECT_EQ(state.records_used(), 0u);
  EXPECT_FALSE(state.panel_open());
  EXPECT_FALSE(state.panel_on_screen());
  EXPECT_FALSE(state.settled());
  EXPECT_EQ(state.serial(), 0u);
}

// ---------------------------------------------------------------------------
// The hotkey, at the funnel
// ---------------------------------------------------------------------------

TEST(AutomapHotkey, TabIsTakenOutOfTheBufferBeforeTheProgramAsks) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.type(key_tab);
  ASSERT_EQ(r.keys_waiting(), 1u);

  r.poll(1);
  EXPECT_EQ(r.keys_waiting(), 0u)
      << "the program polls and the queue is as it would have been";
  EXPECT_TRUE(r.map_state().panel_open());
}

TEST(AutomapHotkey, WithTheSeamOffTabIsTheProgramsKey) {
  const rig r;
  r.adventuring(7, 5, lane_north);
  r.type(key_tab);
  r.poll(1);
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_FALSE(r.map_state().panel_open());
}

TEST(AutomapHotkey, OnlyTheHeadIsClaimedSoOrderAndCountAreKept) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.type(0x1E61);  // an ordinary letter, first
  r.type(key_tab);

  r.poll(1);
  EXPECT_EQ(r.keys_waiting(), 2u) << "the Tab is not at the head yet";
  EXPECT_FALSE(r.map_state().panel_open());
  EXPECT_EQ(r.word_at(bda::segment,
                      r.word_at(bda::segment, bda::keyboard_buffer_head)),
            0x1E61);
}

TEST(AutomapHotkey, ACharacterAloneIsNotEnoughToBeThisSeamsKey) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.type(0x1709);  // Ctrl-I: the same character, the program's key
  r.poll(1);
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_FALSE(r.map_state().panel_open());
}

TEST(AutomapHotkey, NothingIsTakenWhileAnExtendedKeyIsHalfDelivered) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.put_byte(rig::dgroup(), data_key_pushback, 0x48);
  r.type(key_tab);
  r.poll(1);
  EXPECT_EQ(r.keys_waiting(), 1u)
      << "the two halves of an extended key have to stay adjacent";
}

TEST(AutomapHotkey, TheReadRoutineClaimsItToo) {
  // An unconditional wait for a key is not preceded by a poll, so the
  // read routine is the only place a Tab typed into one can be taken.
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.type(key_tab);
  r.stand_on(r.point(1));
  r.pc().step();
  EXPECT_EQ(r.keys_waiting(), 0u);
  EXPECT_TRUE(r.map_state().panel_open());
}

TEST(AutomapHotkey, ADataSegmentThatIsNotTheProgramsIsDeclined) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.type(key_tab);
  r.stand_on(r.point(0));
  r.regs()[cpu::sreg::ds] = 0x1234;
  r.pc().step();

  EXPECT_EQ(r.keys_waiting(), 1u) << "declined, and nothing touched";
  EXPECT_FALSE(r.map_state().panel_open());
  const seam_status status = r.pc().seams().status("automap");
  EXPECT_GT(status.declined, 0u);
}

// ---------------------------------------------------------------------------
// Watching the party
// ---------------------------------------------------------------------------

TEST(AutomapWalk, SightCarriesForwardAndSidewaysUntilAWallStopsIt) {
  const rig r;
  r.enable();
  // An open sixteen-by-sixteen field with a wall two cells north of the
  // party, facing north from (7, 5).
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 3, lane_north);

  r.poll(4);
  const automap_record* map = r.map_state().find(3, 0, 3);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 7, 5)) << "where it stands";
  EXPECT_TRUE(automap_state::seen(*map, 7, 4));
  EXPECT_TRUE(automap_state::seen(*map, 7, 3)) << "up to the wall";
  EXPECT_FALSE(automap_state::seen(*map, 7, 2)) << "and not past it";
  EXPECT_TRUE(automap_state::seen(*map, 6, 5)) << "and to each side";
  EXPECT_TRUE(automap_state::seen(*map, 8, 5));
  EXPECT_FALSE(automap_state::seen(*map, 5, 5)) << "but not two aside";
}

TEST(AutomapWalk, AWallOnTheFarCellStopsSightJustTheSame) {
  // The program's data often records a border's wall on one side only.
  // Checking the near face alone let sight leak through it.
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 4, lane_south, 1, 0);  // the far cell's face, and nothing else

  r.poll(4);
  const automap_record* map = r.map_state().find(3, 0, 3);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 7, 5));
  EXPECT_FALSE(automap_state::seen(*map, 7, 4));
}

TEST(AutomapWalk, SightDoesNotWrapRoundTheEdgeOfTheMap) {
  const rig r;
  r.enable();
  r.adventuring(7, 0, lane_north);  // on the top row, looking off the map
  r.poll(4);
  const automap_record* map = r.map_state().find(3, 0, 3);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 7, 0));
  EXPECT_FALSE(automap_state::seen(*map, 7, 15))
      << "the grid wraps; what a player can see does not";
}

TEST(AutomapWalk, NothingIsBelievedWhileTheProgramIsMovingTheParty) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.put_byte(rig::dgroup(), data_in_transition, 1);
  r.poll(6);
  EXPECT_EQ(r.map_state().records_used(), 0u);
  EXPECT_FALSE(r.map_state().settled());
}

TEST(AutomapWalk, NothingIsWatchedOffTheAdventuringScreen) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.put_byte(rig::dgroup(), data_game_mode, 5);  // a fight
  r.poll(6);
  EXPECT_EQ(r.map_state().records_used(), 0u);
}

TEST(AutomapWalk, AnOverlandViewIsNotThisPanelsMap) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.put_byte(rig::dgroup(), data_view_kind, 2);
  r.poll(6);
  EXPECT_EQ(r.map_state().records_used(), 0u);
}

TEST(AutomapWalk, AMapPointerOutsideConventionalMemoryIsDeclined) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  // The video window. Reading it would load the adapter's latches, which
  // is a seam changing the machine in order to look at it.
  r.put_word(rig::dgroup(), static_cast<std::uint16_t>(data_map_pointer + 2),
             0xA000);
  r.poll(4);
  EXPECT_EQ(r.map_state().records_used(), 0u);
  EXPECT_GT(r.pc().seams().status("automap").declined, 0u);
}

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------

TEST(AutomapPanel, ClosedItDrawsNothingAtAll) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(8);

  EXPECT_GT(r.map_state().records_used(), 0u) << "it still learns the map";
  for (unsigned y = 0; y < automap_panel_height; y += 8) {
    for (unsigned x = 0; x < automap_panel_width; x += 8) {
      ASSERT_EQ(r.screen_pixel(automap_panel_x + x, automap_panel_y + y), 0)
          << "the panel is closed; nothing of it is on the screen";
    }
  }
}

TEST(AutomapPanel, TabPutsItOnTheGamesOwnScreen) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  ASSERT_TRUE(r.map_state().panel_open());
  ASSERT_TRUE(r.map_state().panel_on_screen());

  // The cell the party is standing on: a white arrow on black, seven
  // pixels a side, at (7, 5) of the grid.
  const unsigned cell_x = automap_panel_x + (7 * automap_cell_pixels);
  const unsigned cell_y = automap_panel_y + (5 * automap_cell_pixels);
  EXPECT_EQ(r.screen_pixel(cell_x + 3, cell_y + 3), 15)
      << "the arrow's stem, in the planes";

  // A cell it has seen, in the ground colour the program says the 3D
  // view's floor is.
  const unsigned seen_x = automap_panel_x + (6 * automap_cell_pixels) + 3;
  const unsigned seen_y = automap_panel_y + (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(r.screen_pixel(seen_x, seen_y), 2);

  // A cell it has not: black.
  const unsigned unseen_x = automap_panel_x + (1 * automap_cell_pixels) + 3;
  const unsigned unseen_y = automap_panel_y + (1 * automap_cell_pixels) + 3;
  EXPECT_EQ(r.screen_pixel(unseen_x, unseen_y), 0);
}

TEST(AutomapPanel, AWallIsAStrokeAndAWayThroughIsAGapInOne) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 5, lane_west);  // solid
  r.face(7, 5, lane_east, 1, 1);    // a way through
  // Something on this map is a shut door, of a *different* kind of wall
  // face — so the panel knows which faces are doors, and knows the one
  // above is not.
  r.face(2, 2, lane_north, 4, 2);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const std::array<std::uint8_t, automap_panel_pixels>& panel =
      r.map_state().pixels();
  const auto pixel = [&panel](unsigned x, unsigned y) {
    return panel[(static_cast<std::size_t>(y) * automap_panel_width) + x];
  };
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = 5 * automap_cell_pixels;

  // West: a full stroke, wall colour all the way down.
  EXPECT_EQ(pixel(x0, y0), 6);
  EXPECT_EQ(pixel(x0, y0 + 3), 6);
  EXPECT_EQ(pixel(x0, y0 + 6), 6);

  // East: a stub at each end and an opening between them.
  const unsigned xe = x0 + automap_cell_pixels - 1;
  EXPECT_EQ(pixel(xe, y0), 6);
  EXPECT_EQ(pixel(xe, y0 + 6), 6);
  EXPECT_EQ(pixel(xe, y0 + 3), 2) << "the opening is floor, not wall";
}

TEST(AutomapPanel, AShutDoorIsAYellowLeafInsideItsOwnWall) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  // On the cell ahead of the party, not the party's own: the arrow sits
  // on a black backdrop five pixels square, and the leaf is two of the
  // seven this cell has.
  r.face(7, 4, lane_west, 4, 2);  // shut: unarguably a door
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const auto pixel = [&panel](unsigned x, unsigned y) {
    return panel[(static_cast<std::size_t>(y) * automap_panel_width) + x];
  };
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = 4 * automap_cell_pixels;

  // The leaf: two pixels thick, inset one at each end.
  EXPECT_EQ(pixel(x0, y0 + 3), 14);
  EXPECT_EQ(pixel(x0 + 1, y0 + 3), 14);
  // The flanks the leaf is set into: the wall's own colour, so the door
  // is a door *within* a wall rather than a break in one.
  EXPECT_EQ(pixel(x0, y0), 6);
  EXPECT_EQ(pixel(x0, y0 + 6), 6);
}

TEST(AutomapPanel, APassableFaceOfAKindSeenShutSomewhereElseIsADoorToo) {
  // The renderer picks a wall's graphic from the face nibble alone, so an
  // open door and a shut one of the same kind draw the same leaf. The
  // evidence for which kinds are doors is the shut ones.
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 5, lane_west, 4, 1);   // passable, kind 4
  r.face(2, 2, lane_north, 4, 2);  // and kind 4 is shut over there
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            14);
  EXPECT_NE(r.map_state().door_nibbles() & (1U << 4U), 0u);
}

TEST(AutomapPanel, TheShippedDataSaysWhichFacesAreDoorsWhereAMapDoesNot) {
  // The evidence is scattered — sub-maps share a wall set and only some
  // of them have a shut instance — so the seam carries a table of every
  // shut face in the shipped data, keyed on (disk, WALLDEF block, row).
  // Here nothing on the map is shut and the table is the only witness.
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 5, lane_west, 5, 1);  // passable, kind 5 = block 1's row 4
  r.put_word(rig::dgroup(), data_wall_set + wall_set_stride, 1);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            14);
}

TEST(AutomapPanel, ASlotFilledFromAMultiBlockLoadIsNoWitnessAtAll) {
  // 0xFFFF is what the loader stamps when it cannot say which block a
  // row came from, and the table cannot be consulted for it.
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 5, lane_west, 5, 1);
  r.face(2, 2, lane_north, 4, 2);  // some other kind is shut, so the mask
                                   // is not empty and the fallback is off
  r.put_word(rig::dgroup(), data_wall_set + wall_set_stride, 0xFFFF);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0], 2)
      << "not a door, so a way through, so the floor shows";
}

TEST(AutomapPanel, WithNoEvidenceAtAllAWayThroughIsDrawnAsADoor) {
  // Nothing shut on the map and nothing in the table for its wall sets.
  // The nibble cannot tell a door from an archway, so the older rule
  // stands: a passable face is a door. Drawing none would be worse.
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 5, lane_west, 4, 1);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  EXPECT_EQ(r.map_state().door_nibbles(), 0u);
  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            14);
}

TEST(AutomapPanel, AShutDoorRecordedOnlyOnTheFarCellStillShows) {
  // Being shut is a property of the border rather than of a side, and
  // four half-edges in the shipped data are recorded on one cell only.
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.face(7, 5, lane_west, 4, 0);  // the near face: a plain solid wall
  r.face(6, 5, lane_east, 4, 2);  // the far face: shut
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            14);
}

TEST(AutomapPanel, AWallIsTheColourOfItsOwnTexture) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 5, lane_west);  // wall face kind 1
  r.wall_texture(1, 9);             // whose tiles are bright blue
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  EXPECT_TRUE(r.map_state().wall_colour_known(1));
  EXPECT_EQ(r.map_state().wall_colour(1), 9)
      << "histogrammed out of the tiles the 3D view blits for it";
  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            9);
}

TEST(AutomapPanel, BlackIsNotAWallColourBecauseItIsTheFogsColour) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 5, lane_west);
  r.wall_texture(1, 0);  // a tile that is entirely black
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  EXPECT_FALSE(r.map_state().wall_colour_known(1));
  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0], 6)
      << "the area's frame colour, which is the fallback";
}

TEST(AutomapPanel, ATileSetThatDoesNotAgreeWithItselfIsRefused) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 5, lane_west);
  r.wall_texture(1, 9);
  // A frame smaller than its own scanlines times its byte columns times
  // the four planes is not a tile set, whatever else it is.
  r.put_word(map_bank_segment, tile_header_frame_bytes, 2);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  EXPECT_FALSE(r.map_state().wall_colour_known(1));
}

TEST(AutomapPanel, AWallTheColourOfTheFloorIsShiftedSoItCanBeSeen) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.put_byte(rig::dgroup(), data_ground_colour, 6);  // the frame colour too
  r.wall_between(7, 5, lane_west);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);

  const auto& panel = r.map_state().pixels();
  const unsigned x0 = 7 * automap_cell_pixels;
  const unsigned y0 = (5 * automap_cell_pixels) + 3;
  EXPECT_EQ(panel[(static_cast<std::size_t>(y0) * automap_panel_width) + x0],
            15)
      << "6 is the floor, its bright twin 14 is the door yellow, so 15";
}

TEST(AutomapPanel, TheColoursAreWorkedOutAgainWhenTheTileSetsAreSwapped) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.wall_between(7, 5, lane_west);
  r.wall_texture(1, 9);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);
  ASSERT_EQ(r.map_state().wall_colour(1), 9);

  // The same map, a different wall set loaded under it: the cached
  // colour was histogrammed out of tiles that are not there any more.
  r.wall_texture(1, 4);
  r.put_word(rig::dgroup(), data_tile_banks + 2, map_bank_segment + 0x100);
  r.put_word(map_bank_segment + 0x100, tile_header_height, 1);
  r.put_word(map_bank_segment + 0x100, tile_header_width, 1);
  r.put_word(map_bank_segment + 0x100, tile_header_frame_bytes, 4);
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.put_byte(map_bank_segment + 0x100,
               static_cast<std::uint16_t>(tile_first_frame + plane),
               ((4U >> plane) & 1U) != 0 ? 0xFF : 0x00);
  }
  r.poll(2);
  EXPECT_EQ(r.map_state().wall_colour(1), 4);
}

TEST(AutomapPanel, AClearThatMeetsThePanelTakesItAndTheRosterGivesItBack) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  // The program clears a box over the roster: a character sheet, an item
  // list, a message. The mode byte still says "adventuring".
  r.stand_on(r.point(2));
  r.put_word(rig::dgroup(), 0x0404, 0x0E);  // bottom
  r.put_word(rig::dgroup(), 0x0406, 0x26);  // right
  r.put_word(rig::dgroup(), 0x0408, 0x01);  // top
  r.put_word(rig::dgroup(), 0x040A, 0x11);  // left
  r.regs()[cpu::reg16::sp] = 0x0400;
  r.pc().step();
  EXPECT_TRUE(r.map_state().panel_covered());
  EXPECT_FALSE(r.map_state().panel_on_screen());

  r.poll(2);
  EXPECT_FALSE(r.map_state().panel_on_screen())
      << "while something else owns those cells the panel stays off them";

  // The program repaints the roster: the cells are the panel's again.
  r.stand_on(r.point(4));
  r.pc().step();
  EXPECT_FALSE(r.map_state().panel_covered());
  r.poll(2);
  EXPECT_TRUE(r.map_state().panel_on_screen());
}

TEST(AutomapPanel, TheStatusRowsOwnClearIsNotACover) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  r.stand_on(r.point(2));
  r.put_word(rig::dgroup(), 0x0404, 0x0F);
  r.put_word(rig::dgroup(), 0x0406, 0x26);
  r.put_word(rig::dgroup(), 0x0408, 0x0F);
  r.put_word(rig::dgroup(), 0x040A, 0x11);
  r.regs()[cpu::reg16::sp] = 0x0400;
  r.pc().step();
  EXPECT_FALSE(r.map_state().panel_covered());
  EXPECT_TRUE(r.map_state().panel_on_screen());
}

TEST(AutomapPanel, TheWholeScreenGoingTakesThePanelWithIt) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  r.stand_on(r.point(3));
  r.pc().step();
  EXPECT_TRUE(r.map_state().panel_covered());
  EXPECT_FALSE(r.map_state().panel_on_screen());
}

TEST(AutomapPanel, ClosingItAsksTheProgramToPutItsOwnScreenBack) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(4);
  r.type(key_tab);
  r.poll(2);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  // The program's screen composer, as far as this test needs it to be:
  // something that writes down the segment it was entered with and
  // returns far. The segment is the point — the real routine reaches its
  // own literals through CS, so a seam that called it at the image base
  // rather than at the paragraph it was linked at would run the same
  // bytes and draw the screen out of the wrong ones.
  //
  //   mov ax, cs ; mov [witness], ax ; retf
  constexpr std::uint16_t witness = 0x0300;
  constexpr std::uint16_t composer_paragraph = 0x0BA;
  constexpr std::uint16_t composer_offset = 0x27D9;
  constexpr std::array<std::uint8_t, 7> composer{0x8C, 0xC8, 0xA3, 0x00,
                                                 0x03, 0xCB, 0x90};
  const std::uint32_t at = cpu::physical_address(
      static_cast<std::uint16_t>(image_load_segment + composer_paragraph),
      composer_offset);
  for (std::size_t i = 0; i < composer.size(); ++i) {
    r.pc().memory().ram()[at + i] = composer[i];
  }
  r.put_word(rig::dgroup(), witness, 0);

  r.type(key_tab);
  r.stand_on(r.point(0));
  const cpu::registers before = r.regs();
  r.pc().step();

  EXPECT_FALSE(r.map_state().panel_open());
  EXPECT_FALSE(r.map_state().panel_on_screen())
      << "marked down before the call is queued, so the point being offered "
         "again cannot queue a second one";

  // Let the batch finish: the composer's far return lands on the engine's
  // sentinel and the snapshot goes back.
  for (int i = 0; i < 6; ++i) {
    r.pc().step();
  }
  EXPECT_EQ(r.word_at(rig::dgroup(), witness),
            static_cast<std::uint16_t>(image_load_segment + composer_paragraph))
      << "the routine ran, and it ran at the segment it was linked at";
  EXPECT_EQ(r.regs()[cpu::sreg::cs], before[cpu::sreg::cs]);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], before[cpu::reg16::sp])
      << "the routine cleaned its own frame and the engine put the rest back";
  EXPECT_FALSE(r.map_state().panel_open());
}

// ---------------------------------------------------------------------------
// The fidelity pair
// ---------------------------------------------------------------------------

TEST(AutomapFidelity, OnAndNeverAskedLeavesTheRunIdentical) {
  // #173's first commit, as a test: the seam most able to break the
  // invariant, on, armed at every one of its five points, with the key
  // never pressed. Every point reads; none of them writes.
  const auto run = [](bool on) {
    const rig r;
    if (on) {
      EXPECT_EQ(r.pc().seams().enable("automap"), seam_reason::none);
      EXPECT_TRUE(r.pc().seams().armed());
    }
    r.adventuring(7, 5, lane_north);

    // Stand on the keyboard-poll point and run the idle program from
    // there, so the point is genuinely reached rather than merely armed.
    const std::uint32_t at = r.point(0);
    for (std::size_t i = 0; i < idle_program.size(); ++i) {
      r.pc().memory().ram()[at + i] = idle_program[i];
    }
    r.stand_on(at);
    r.pc().memory().ram()[at] = idle_program[0];
    for (int i = 0; i < 4; ++i) {
      r.pc().step();
    }
    return hash_state(r.pc());
  };

  EXPECT_EQ(run(false), run(true));
}

TEST(AutomapFidelity, WhatItLearnsIsNotMachineState) {
  // The panel is closed, so nothing is drawn — but the map is being
  // learned all the same, and a machine that has learned one has to hash
  // as the machine that has not.
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  const state_hashes before = hash_state(r.pc());
  r.poll(8);
  ASSERT_GT(r.map_state().records_used(), 0u);

  const rig plain;
  plain.adventuring(7, 5, lane_north);
  plain.poll(8);
  EXPECT_EQ(hash_state(r.pc()), hash_state(plain.pc()));
  EXPECT_NE(before, hash_state(r.pc()))
      << "the run did happen; it is the exploration that is not in the hash";
}

TEST(AutomapFidelity, AResetMachineHasExploredNothing) {
  const rig r;
  r.enable();
  r.adventuring(7, 5, lane_north);
  r.poll(8);
  ASSERT_GT(r.map_state().records_used(), 0u);
  r.pc().reset();
  EXPECT_EQ(r.map_state().records_used(), 0u);
  EXPECT_FALSE(r.map_state().panel_open());
}

TEST(AutomapDefinition, ItIsACommandAndNotAPullAndItNamesTheBaseline) {
  const rig r;
  const seam_definition* automap = r.pc().seams().find("automap");
  ASSERT_NE(automap, nullptr);
  EXPECT_FALSE(automap->trigger) << "a key is not a pull";
  EXPECT_EQ(automap->gate, document_kind::none);
  EXPECT_EQ(automap->schema, seam_schema_version);
  EXPECT_EQ(automap->points.size(), 5u);
  for (const seam_point& point : automap->points) {
    EXPECT_FALSE(point.at_every_step) << "every point of this seam has an "
                                         "address, which is where its safety "
                                         "comes from";
    EXPECT_EQ(point.module.file, resident_image.file);
  }
  ASSERT_EQ(automap->fingerprints.size(), 1u);
  EXPECT_EQ(automap->fingerprints.front(),
            known_editions().front().fingerprint);
}

}  // namespace
}  // namespace amberfolio::machine
