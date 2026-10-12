// SPDX-License-Identifier: AGPL-3.0-only
//
// The scene read (scene.h, #520): where the party is and what is around
// it, read out of RAM.
//
// Every map here is this test's own: a few faces set by hand in the four
// planes the program's map format has, which is what the read is a reader
// of. What is under test is that format, the door rule the panel draws
// with, the sight rule, the guards, and the JSON a host is handed.

#include "amberfolio/machine/scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/log.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

// --- The facts the read is a reader of (docs/seams.md §10,
// docs/explored-overlay.md §2) ---------------------------------------------

constexpr std::uint16_t image_segment = 0x1000;
constexpr std::uint32_t dgroup_offset = 0xC7C0;

constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint16_t data_in_transition = 0x442F;
constexpr std::uint16_t data_disk = 0x5376;
constexpr std::uint16_t data_area = 0x84DC;
constexpr std::uint16_t data_x = 0x6AAD;
constexpr std::uint16_t data_y = 0x6AAE;
constexpr std::uint16_t data_facing = 0x6AAF;
constexpr std::uint16_t data_map = 0x6A5C;
constexpr std::uint16_t data_record = 0x49D2;
constexpr std::uint16_t data_wall_set = 0x6AAE;
constexpr std::uint16_t data_terrain = 0x3648;
constexpr std::uint16_t data_bias = 0x3C76;

constexpr std::uint16_t record_geo = 0x18A;
constexpr std::uint16_t record_column = 0x186;
constexpr std::uint16_t record_row = 0x188;
constexpr std::uint16_t record_3d = 0x1CC;

/// Anywhere in conventional memory the record and the map can sit.
constexpr std::uint16_t record_segment = 0x5000;
constexpr std::uint16_t map_segment = 0x5100;

// The tactical grid's (docs/hosts.md §13).
constexpr std::uint16_t data_slot_records = 0x65B9;
constexpr std::uint16_t data_slot_squares = 0x5F27;
constexpr std::uint16_t data_actor = 0x5D92;
constexpr std::uint16_t data_body_count = 0x6715;
constexpr std::uint16_t data_bodies = 0x66D6;
constexpr std::uint16_t data_combat_block = 0x6716;
constexpr std::uint16_t data_ground_cost = 0x2744;
constexpr std::uint16_t record_status = 0x10C;
constexpr std::uint16_t record_in_fight = 0x10D;
constexpr std::uint16_t record_side = 0x10E;
constexpr std::uint16_t record_computer = 0x10F;
constexpr std::uint16_t record_ac = 0x111;
constexpr std::uint16_t record_hp = 0x11B;
constexpr std::uint16_t combat_record_bytes = 0x120;
constexpr std::uint8_t ground_open = 0x17;
constexpr std::uint16_t combat_block_segment = 0x5200;
constexpr std::uint16_t combat_records_segment = 0x5400;

constexpr unsigned north = 0;
constexpr unsigned east = 2;
constexpr unsigned south = 4;
constexpr unsigned west = 6;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {}

  void load_baseline() const {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, digest));
    box->seams().loaded(digest, image_segment);
  }

  [[nodiscard]] std::span<std::uint8_t> ram() const {
    return box->memory().ram();
  }
  [[nodiscard]] static std::uint32_t data_base() {
    return (std::uint32_t{image_segment} * 16U) + dgroup_offset;
  }

  void data(std::uint16_t offset, std::uint8_t value) const {
    ram()[data_base() + offset] = value;
  }
  void data_word(std::uint16_t offset, std::uint16_t value) const {
    data(offset, static_cast<std::uint8_t>(value & 0xFFU));
    data(static_cast<std::uint16_t>(offset + 1),
         static_cast<std::uint8_t>(value >> 8U));
  }
  void record_word(std::uint16_t offset, std::uint16_t value) const {
    const std::uint32_t at = (std::uint32_t{record_segment} * 16U) + offset;
    ram()[at] = static_cast<std::uint8_t>(value & 0xFFU);
    ram()[at + 1] = static_cast<std::uint8_t>(value >> 8U);
  }

  /// One face of one square: its wall-face nibble and its two style bits
  /// (0 solid, 1 a way through, 2 shut).
  void face(unsigned x, unsigned y, unsigned lane, std::uint8_t nibble,
            std::uint8_t style) const {
    const std::uint32_t base = std::uint32_t{map_segment} * 16U;
    const unsigned cell = (y * 16U) + x;
    std::uint8_t& faces = ram()[base + (lane < 4 ? 0U : 0x100U) + cell];
    const bool high = lane == north || lane == south;
    faces = high ? static_cast<std::uint8_t>((faces & 0x0FU) | (nibble << 4U))
                 : static_cast<std::uint8_t>((faces & 0xF0U) | nibble);
    std::uint8_t& styles = ram()[base + 0x300U + cell];
    styles = static_cast<std::uint8_t>((styles & ~(3U << lane)) |
                                       (unsigned{style} << lane));
  }

  /// The interior screen, with the party at (x, y) facing `facing` on
  /// disk 3, area 0.
  void interior(std::uint8_t x, std::uint8_t y, std::uint8_t facing) const {
    data(data_game_mode, 4);
    data(data_view_kind, 1);
    data(data_disk, 3);
    data(data_area, 0);
    data(data_x, x);
    data(data_y, y);
    data(data_facing, facing);
    data_word(data_record, 0);
    data_word(data_record + 2, record_segment);
    data_word(data_map, 0);
    data_word(data_map + 2, map_segment);
    // No wall set loaded that the door table names.
    for (std::uint16_t slot = 1; slot <= 3; ++slot) {
      data_word(static_cast<std::uint16_t>(data_wall_set + (slot * 4U)),
                0xFFFF);
    }
  }

  /// The travel view on the first wilderness area, the party at (column,
  /// row) facing south, and every terrain byte of the table its own index
  /// in a row so a cell says where it was read from.
  void travel(std::uint16_t column, std::uint16_t row) const {
    data(data_game_mode, 3);
    data(data_view_kind, 2);
    data(data_disk, 6);
    data(data_area, 25);
    data(data_facing, 4);
    data(data_bias + 2, 0);
    data_word(data_record, 0);
    data_word(data_record + 2, record_segment);
    record_word(record_column, column);
    record_word(record_row, row);
    record_word(record_3d, 0);
    for (std::uint16_t r = 0; r < 36; ++r) {
      for (std::uint16_t c = 0; c < 44; ++c) {
        data(static_cast<std::uint16_t>(data_terrain + (r * 44U) + c),
             static_cast<std::uint8_t>(c));
      }
    }
  }

  [[nodiscard]] static std::uint32_t record_at(unsigned slot) {
    return (std::uint32_t{combat_records_segment} * 16U) +
           (slot * combat_record_bytes);
  }
  void combat_record_byte(unsigned slot, std::uint16_t offset,
                          std::uint8_t value) const {
    ram()[record_at(slot) + offset] = value;
  }
  void far_pointer(std::uint16_t offset, std::uint32_t linear) const {
    data_word(offset, static_cast<std::uint16_t>(linear & 0x0FU));
    data_word(static_cast<std::uint16_t>(offset + 2),
              static_cast<std::uint16_t>(linear >> 4U));
  }

  /// A fight with no one in it yet: the window's top-left at (x, y), every
  /// square open ground that costs one, and that ground's cost set.
  void combat(std::uint8_t x, std::uint8_t y) const {
    data(data_game_mode, 5);
    data(data_disk, 6);
    data(data_area, 25);
    data_word(data_combat_block, 0);
    data_word(data_combat_block + 2, combat_block_segment);
    const std::uint32_t block = std::uint32_t{combat_block_segment} * 16U;
    ram()[block + 2] = x;
    ram()[block + 3] = y;
    for (unsigned i = 0; i < 50U * 25U; ++i) {
      ram()[block + 7 + i] = ground_open;
    }
    data(static_cast<std::uint16_t>(data_ground_cost + (ground_open * 4U)), 1);
    data(data_slot_squares + 3, 0);
  }

  /// One figure in the next slot: its name, side, square and shape, in the
  /// fight and okay, commanded by the player if on the party's side.
  unsigned figure(std::string_view name, bool enemy, std::uint8_t x,
                  std::uint8_t y, std::uint8_t shape = 1) const {
    const unsigned slot =
        static_cast<unsigned>(ram()[data_base() + data_slot_squares + 3]) + 1U;
    data(data_slot_squares + 3, static_cast<std::uint8_t>(slot));
    combat_record_byte(slot, 0, static_cast<std::uint8_t>(name.size()));
    for (std::size_t i = 0; i < name.size(); ++i) {
      combat_record_byte(slot, static_cast<std::uint16_t>(1 + i),
                         static_cast<std::uint8_t>(name[i]));
    }
    combat_record_byte(slot, record_in_fight, 1);
    combat_record_byte(slot, record_side, enemy ? 1 : 0);
    combat_record_byte(slot, record_computer, enemy ? 1 : 0);
    combat_record_byte(slot, record_ac, 0x3C - 4);
    combat_record_byte(slot, record_hp, 20);
    far_pointer(static_cast<std::uint16_t>(data_slot_records + (slot * 4U)),
                record_at(slot));
    const auto squares =
        static_cast<std::uint16_t>(data_slot_squares + (slot * 4U));
    data(squares, x);
    data(static_cast<std::uint16_t>(squares + 1), y);
    data(static_cast<std::uint16_t>(squares + 2),
         static_cast<std::uint8_t>(slot));
    data(static_cast<std::uint16_t>(squares + 3), shape);
    return slot;
  }

  void acting(unsigned slot) const { far_pointer(data_actor, record_at(slot)); }

  [[nodiscard]] scene read() const {
    scene out{};
    last = read_scene(*box, out);
    return out;
  }

  [[nodiscard]] std::string json() const {
    scene now{};
    (void)read_scene(*box, now);
    std::string text(scene_json(now, {}) + 1, '\0');
    text.resize(scene_json(now, text));
    return text;
  }

  diagnostic_log log;
  std::unique_ptr<machine> box;
  mutable scene_trouble last{scene_trouble::none};
};

/// The square at (x, y) among the first `count` of `in`, or null.
[[nodiscard]] const scene_square* square(std::span<const scene_square> in,
                                         std::size_t count, unsigned x,
                                         unsigned y) {
  for (std::size_t i = 0; i < count; ++i) {
    if (in[i].x == x && in[i].y == y) {
      return &in[i];
    }
  }
  return nullptr;
}

// --- Nothing known ----------------------------------------------------------

TEST(Scene, NoProgramIsNoScene) {
  const rig r;
  const scene s = r.read();
  EXPECT_EQ(r.last, scene_trouble::no_program);
  EXPECT_EQ(s.screen, scene_screen::none);
  EXPECT_EQ(r.json(), "null");
}

TEST(Scene, AProgramThisBuildHasNoFactsForIsNoScene) {
  const rig r;
  sha256_digest other{};
  other.bytes[0] = 0x5A;
  r.box->seams().loaded(other, image_segment);
  r.interior(5, 5, north);
  (void)r.read();
  EXPECT_EQ(r.last, scene_trouble::wrong_binary);
  EXPECT_STREQ(scene_trouble_name(scene_trouble::wrong_binary), "wrong_binary");
}

TEST(Scene, AScreenNoShapeCoversIsNoScene) {
  const rig r;
  r.load_baseline();
  r.data(data_game_mode, 2);  // the camp
  EXPECT_EQ(r.read().screen, scene_screen::none);
  EXPECT_EQ(r.last, scene_trouble::elsewhere);
  EXPECT_EQ(r.json(), "null");
}

TEST(Scene, AScriptedMoveIsNoScene) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  r.data(data_in_transition, 1);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

TEST(Scene, AMapNotLoadedIsNoScene) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  r.data_word(data_map + 2, 0);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

TEST(Scene, AFacingThatIsNotACardinalIsNoScene) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, 3);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

// --- The interior screen ----------------------------------------------------

TEST(SceneExplore, SaysWhereThePartyIsAndWhatTheMapIsCalled) {
  const rig r;
  r.load_baseline();
  r.interior(7, 5, east);
  const scene s = r.read();
  ASSERT_EQ(s.screen, scene_screen::explore);
  EXPECT_EQ(s.x, 7);
  EXPECT_EQ(s.y, 5);
  EXPECT_EQ(s.facing, east);
  EXPECT_EQ(std::string_view(s.zone.data(), s.zone_length), "NEW PHLAN");
}

TEST(SceneExplore, AZoneNameLosesItsSoftBreaks) {
  const rig r;
  r.load_baseline();
  r.interior(7, 5, east);
  r.data(data_disk, 6);
  r.data(data_area, 1);
  const scene s = r.read();
  EXPECT_EQ(std::string_view(s.zone.data(), s.zone_length), "BUCCANEER BASE");
}

TEST(SceneExplore, AMapWithNoNameIsItsNumber) {
  const rig r;
  r.load_baseline();
  r.interior(7, 5, east);
  r.data(data_area, 99);
  const scene s = r.read();
  EXPECT_EQ(std::string_view(s.zone.data(), s.zone_length), "AREA 99");
}

TEST(SceneExplore, TellsTheFiveKindsOfFaceApart) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  // A shut face of kind 3 elsewhere on the map is the evidence that kind 3
  // is a door; kind 5, passable and never seen shut, is then an opening.
  r.face(0, 0, north, 3, 2);
  r.face(5, 5, north, 1, 0);  // a wall
  r.face(5, 5, east, 3, 1);   // a door, open
  r.face(5, 5, south, 5, 1);  // an opening
  r.face(5, 5, west, 3, 2);   // a shut door
  const scene s = r.read();
  ASSERT_GE(s.view_count, 1);
  const scene_square& here = s.view[0];
  EXPECT_EQ(here.x, 5);
  EXPECT_EQ(here.y, 5);
  EXPECT_EQ(here.face(north), scene_face::wall);
  EXPECT_EQ(here.face(east), scene_face::door);
  EXPECT_EQ(here.face(south), scene_face::opening);
  EXPECT_EQ(here.face(west), scene_face::shut);
}

TEST(SceneExplore, AMapNothingIsKnownAboutCallsEveryWayThroughADoor) {
  // The panel's pre-nibble rule: no shut face anywhere and no wall set the
  // table names, so a passable face is a door rather than an arch.
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  r.face(5, 5, east, 5, 1);
  EXPECT_EQ(r.read().view[0].face(east), scene_face::door);
}

TEST(SceneExplore, AWallRecordedOnTheFarSideIsAWall) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  r.face(6, 5, west, 1, 0);   // only the neighbour records the border
  r.face(5, 6, north, 3, 2);  // a shut door, only on the far side
  const scene s = r.read();
  EXPECT_EQ(s.view[0].face(east), scene_face::wall);
  EXPECT_EQ(s.view[0].face(south), scene_face::shut);
}

TEST(SceneExplore, TheEdgeOfTheMapBorrowsNothingFromTheOtherEdge) {
  const rig r;
  r.load_baseline();
  r.interior(0, 5, north);
  r.face(15, 5, east, 1, 0);
  EXPECT_EQ(r.read().view[0].face(west), scene_face::open);
}

TEST(SceneExplore, AroundIsThePartysOwnSquareTurnedToItsFacing) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, east);
  r.face(5, 5, east, 1, 0);
  r.face(5, 5, north, 3, 2);
  const std::string json = r.json();
  EXPECT_NE(json.find(R"("facing":"east")"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("around":{"ahead":"wall","right":"open",)"
                      R"("behind":"open","left":"shut"})"),
            std::string::npos)
      << json;
}

TEST(SceneExplore, TheViewIsWhatSightReaches) {
  const rig r;
  r.load_baseline();
  r.interior(5, 10, north);
  // Open ahead for two squares and then a wall; a wall on the left of the
  // first square ahead, so sight does not reach past it.
  r.face(5, 8, north, 1, 0);
  r.face(5, 9, west, 1, 0);
  const scene s = r.read();
  const std::span<const scene_square> view(s.view);
  // Depth 0: (5,10) and both sides; depth 1: (5,9) and its right only;
  // depth 2: (5,8) and both sides; the wall ahead of it ends sight.
  EXPECT_EQ(s.view_count, 8);
  EXPECT_EQ(s.view[0].x, 5);
  EXPECT_EQ(s.view[0].y, 10);
  EXPECT_NE(square(view, s.view_count, 4, 10), nullptr);
  EXPECT_NE(square(view, s.view_count, 6, 9), nullptr);
  EXPECT_EQ(square(view, s.view_count, 4, 9), nullptr);
  EXPECT_NE(square(view, s.view_count, 5, 8), nullptr);
  EXPECT_EQ(square(view, s.view_count, 5, 7), nullptr);
}

TEST(SceneExplore, SightDoesNotGoThroughADoorway) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  r.face(5, 5, north, 3, 1);
  const scene s = r.read();
  const std::span<const scene_square> view(s.view);
  EXPECT_EQ(square(view, s.view_count, 5, 4), nullptr);
  EXPECT_EQ(s.view[0].face(north), scene_face::door);
}

TEST(SceneExplore, TheMapIsNullWhileTheAutomapIsOff) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  // Explored in the store, but nobody is keeping it.
  automap_state& store = r.box->automap();
  (void)store.reveal(store.record_for(3, 0, 0), 5, 5);
  const scene s = r.read();
  EXPECT_FALSE(s.map_kept);
  EXPECT_EQ(s.map_count, 0);
  EXPECT_NE(r.json().find(R"("map":null)"), std::string::npos);
}

TEST(SceneExplore, TheMapIsEveryRevealedSquareWhileTheAutomapIsOn) {
  const rig r;
  r.load_baseline();
  ASSERT_EQ(r.box->seams().enable("automap"), seam_reason::none);
  r.interior(5, 5, north);
  r.face(5, 4, east, 1, 0);
  // The loaded geometry block is the third byte of the map's identity.
  r.record_word(record_geo, 2);
  automap_state& store = r.box->automap();
  automap_record& map = store.record_for(3, 0, 2);
  (void)store.reveal(map, 5, 5);
  (void)store.reveal(map, 5, 4);
  // Another grid under the same area is not this one.
  (void)store.reveal(store.record_for(3, 0, 0), 9, 9);

  const scene s = r.read();
  EXPECT_TRUE(s.map_kept);
  ASSERT_EQ(s.map_count, 2);
  // Row by row: (5,4) before (5,5).
  EXPECT_EQ(s.map[0].y, 4);
  EXPECT_EQ(s.map[0].face(east), scene_face::wall);
  EXPECT_EQ(s.map[1].y, 5);
  EXPECT_NE(r.json().find(R"("map":[{"x":5,"y":4,"north":"open",)"
                          R"("east":"wall","south":"open","west":"open"},)"),
            std::string::npos)
      << r.json();
}

TEST(SceneExplore, AnUnexploredMapIsAnEmptyListWhileTheAutomapIsOn) {
  const rig r;
  r.load_baseline();
  ASSERT_EQ(r.box->seams().enable("automap"), seam_reason::none);
  r.interior(5, 5, north);
  EXPECT_NE(r.json().find(R"("map":[])"), std::string::npos) << r.json();
}

// --- The overland -----------------------------------------------------------

TEST(SceneTravel, SaysTheColumnRowAndWindow) {
  const rig r;
  r.load_baseline();
  r.travel(3, 32);
  const scene s = r.read();
  ASSERT_EQ(s.screen, scene_screen::travel);
  EXPECT_EQ(s.x, 3);
  EXPECT_EQ(s.y, 32);
  EXPECT_EQ(s.facing, south);
  EXPECT_EQ(std::string_view(s.zone.data(), s.zone_length), "WILDERNESS");
  // The program's own clamps: the party in the middle cell.
  EXPECT_EQ(s.window_x, 1);
  EXPECT_EQ(s.window_y, 30);
  // Each terrain byte is its column in the table.
  EXPECT_EQ(s.window[0].terrain, 1);
  EXPECT_EQ(s.window[4].terrain, 5);
}

TEST(SceneTravel, AgainstTheEdgeTheWindowStopsWhereTheProgramStopsIt) {
  const rig r;
  r.load_baseline();
  r.travel(0, 35);
  const scene s = r.read();
  EXPECT_EQ(s.window_x, 0);
  EXPECT_EQ(s.window_y, 31);
}

TEST(SceneTravel, SaysWhatTheMoveStepDoesWithEachCell) {
  const rig r;
  r.load_baseline();
  r.travel(3, 32);
  r.data(static_cast<std::uint16_t>(data_terrain + (30U * 44U) + 1U), 0x4E);
  r.data(static_cast<std::uint16_t>(data_terrain + (30U * 44U) + 2U), 0x66);
  r.data(static_cast<std::uint16_t>(data_terrain + (30U * 44U) + 3U), 0x65);
  const scene s = r.read();
  EXPECT_EQ(s.window[0].move, scene_move::blocked);
  EXPECT_EQ(s.window[1].move, scene_move::river);
  EXPECT_EQ(s.window[2].move, scene_move::open);
  EXPECT_NE(r.json().find(R"("rows":[[{"terrain":78,"move":"blocked"},)"
                          R"({"terrain":102,"move":"river"},)"),
            std::string::npos)
      << r.json();
}

TEST(SceneTravel, TheWildernessDrawnIn3DIsNoScene) {
  const rig r;
  r.load_baseline();
  r.travel(3, 32);
  r.record_word(record_3d, 1);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

TEST(SceneTravel, APositionPastTheProgramsClampIsNoScene) {
  const rig r;
  r.load_baseline();
  r.travel(3, 36);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

// --- A fight ----------------------------------------------------------------

TEST(SceneCombat, ListsWhoIsInTheFightAndWhere) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.figure("HULK", false, 12, 8);
  r.figure("ORC", true, 14, 6);
  const scene s = r.read();
  ASSERT_EQ(r.last, scene_trouble::none);
  ASSERT_EQ(s.screen, scene_screen::combat);
  EXPECT_EQ(s.window_x, 10);
  EXPECT_EQ(s.window_y, 5);
  ASSERT_EQ(s.combatant_count, 2U);
  const scene_combatant& hulk = s.combatants[0];
  EXPECT_EQ(std::string_view(hulk.name.data(), hulk.name_length), "HULK");
  EXPECT_EQ(hulk.side, scene_side::party);
  EXPECT_TRUE(hulk.player);
  EXPECT_TRUE(hulk.standing);
  EXPECT_TRUE(hulk.placed);
  EXPECT_EQ(hulk.x, 12U);
  EXPECT_EQ(hulk.y, 8U);
  const scene_combatant& orc = s.combatants[1];
  EXPECT_EQ(orc.side, scene_side::enemy);
  EXPECT_FALSE(orc.player);
  EXPECT_EQ(orc.x, 14U);
  EXPECT_EQ(orc.y, 6U);
}

TEST(SceneCombat, AnEnemyOutsideTheWindowIsNotListedAndAPartyMemberIs) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.figure("HULK", false, 30, 20);
  r.figure("ORC", true, 17, 5);
  r.figure("GOBLIN", true, 16, 11);
  const scene s = r.read();
  ASSERT_EQ(s.combatant_count, 2U);
  EXPECT_EQ(s.combatants[0].x, 30U);
  EXPECT_EQ(std::string_view(s.combatants[1].name.data(),
                             s.combatants[1].name_length),
            "GOBLIN");
}

TEST(SceneCombat, AFigureHalfInTheWindowIsInIt) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  // Two by two, its top-left one square west of the window.
  r.figure("DRAGON", true, 9, 6, 4);
  const scene s = r.read();
  ASSERT_EQ(s.combatant_count, 1U);
  EXPECT_EQ(s.combatants[0].width, 2U);
  EXPECT_EQ(s.combatants[0].height, 2U);
}

TEST(SceneCombat, TheShapesSayHowManySquaresAFigureCovers) {
  const rig r;
  r.load_baseline();
  r.combat(0, 0);
  r.figure("TALL", true, 1, 1, 2);
  r.figure("WIDE", true, 3, 1, 3);
  const scene s = r.read();
  ASSERT_EQ(s.combatant_count, 2U);
  EXPECT_EQ(s.combatants[0].width, 1U);
  EXPECT_EQ(s.combatants[0].height, 2U);
  EXPECT_EQ(s.combatants[1].width, 2U);
  EXPECT_EQ(s.combatants[1].height, 1U);
}

TEST(SceneCombat, AFallenEnemyHasLeftTheList) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  const unsigned orc = r.figure("ORC", true, 12, 6);
  r.combat_record_byte(orc, record_in_fight, 0);
  r.data(static_cast<std::uint16_t>(data_slot_squares + (orc * 4U) + 3U), 0);
  EXPECT_EQ(r.read().combatant_count, 0U);
}

TEST(SceneCombat, AFallenPartyMemberLiesWhereItFell) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  const unsigned thief = r.figure("THIEF", false, 12, 6);
  r.combat_record_byte(thief, record_in_fight, 0);
  r.combat_record_byte(thief, record_status, 4);
  r.data(static_cast<std::uint16_t>(data_slot_squares + (thief * 4U) + 3U), 0);
  r.data(data_body_count, 1);
  r.far_pointer(data_bodies, rig::record_at(thief));
  r.data(data_bodies + 4, 13);
  r.data(data_bodies + 5, 7);
  const scene s = r.read();
  ASSERT_EQ(s.combatant_count, 1U);
  EXPECT_FALSE(s.combatants[0].standing);
  EXPECT_EQ(s.combatants[0].status, scene_status::unconscious);
  EXPECT_TRUE(s.combatants[0].placed);
  EXPECT_EQ(s.combatants[0].x, 13U);
  EXPECT_EQ(s.combatants[0].y, 7U);
}

TEST(SceneCombat, SaysWhoseTurnItIsAndWhetherThePlayerIsAsked) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  const unsigned hulk = r.figure("HULK", false, 12, 8);
  const unsigned orc = r.figure("ORC", true, 14, 6);
  EXPECT_EQ(r.read().turn, -1);

  r.acting(hulk);
  scene s = r.read();
  EXPECT_EQ(s.turn, 0);
  EXPECT_TRUE(s.waiting);

  // Handed to the computer (QUICK): its turn, and nobody asked.
  r.combat_record_byte(hulk, record_computer, 1);
  s = r.read();
  EXPECT_EQ(s.turn, 0);
  EXPECT_FALSE(s.waiting);

  r.acting(orc);
  s = r.read();
  EXPECT_EQ(s.turn, 1);
  EXPECT_FALSE(s.waiting);
}

TEST(SceneCombat, HealthIsCarriedWhereTheScreenPrintsIt) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.figure("HULK", false, 12, 8);
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.combat_record_byte(orc, record_ac, 0x3C + 2);
  scene s = r.read();
  EXPECT_TRUE(s.combatants[0].health_shown);
  EXPECT_EQ(s.combatants[0].hp, 20U);
  EXPECT_EQ(s.combatants[0].ac, 4);
  // An enemy's only while it is the one acting, whose sheet the screen
  // shows.
  EXPECT_FALSE(s.combatants[1].health_shown);
  EXPECT_EQ(r.json().find(R"("name":"ORC","side":"enemy","player":false,)"
                          R"("standing":true,"status":"okay","x":14,"y":6,)"
                          R"("width":1,"height":1})"),
            r.json().find(R"("name":"ORC")"))
      << r.json();

  r.acting(orc);
  s = r.read();
  EXPECT_TRUE(s.combatants[1].health_shown);
  EXPECT_EQ(s.combatants[1].ac, -2);
}

TEST(SceneCombat, TheGroundSaysWhatEachSquareCosts) {
  const rig r;
  r.load_baseline();
  r.combat(0, 0);
  r.figure("HULK", false, 20, 20);
  const std::uint32_t block = std::uint32_t{combat_block_segment} * 16U;
  r.ram()[block + 7 + 1] = 0;      // off the grid
  r.ram()[block + 7 + 2] = 0x38;   // a bush
  r.ram()[block + 7 + 50] = 0x30;  // rough ground
  r.data(static_cast<std::uint16_t>(data_ground_cost + (0x38 * 4U)), 0xFF);
  r.data(static_cast<std::uint16_t>(data_ground_cost + (0x30 * 4U)), 2);
  const scene s = r.read();
  EXPECT_EQ(s.ground[0].move, scene_move::open);
  EXPECT_EQ(s.ground[0].cost, 1U);
  EXPECT_EQ(s.ground[1].move, scene_move::edge);
  EXPECT_EQ(s.ground[2].move, scene_move::blocked);
  EXPECT_EQ(s.ground[7].terrain, 0x30U);
  EXPECT_EQ(s.ground[7].cost, 2U);
  EXPECT_NE(r.json().find(R"("rows":[[{"terrain":23,"move":"open","cost":1},)"
                          R"({"terrain":0,"move":"edge"},)"
                          R"({"terrain":56,"move":"blocked"},)"),
            std::string::npos)
      << r.json();
}

TEST(SceneCombat, AWindowOffTheGridIsNoScene) {
  const rig r;
  r.load_baseline();
  r.combat(44, 0);
  r.figure("HULK", false, 1, 1);
  EXPECT_EQ(r.read().screen, scene_screen::none);
  r.combat(0, 19);
  r.figure("HULK", false, 1, 1);
  EXPECT_EQ(r.read().screen, scene_screen::none);
  r.combat(43, 18);
  r.figure("HULK", false, 1, 1);
  EXPECT_EQ(r.read().screen, scene_screen::combat);
}

TEST(SceneCombat, NoBlockOrNoSlotsIsNoScene) {
  const rig r;
  r.load_baseline();
  r.combat(0, 0);
  EXPECT_EQ(r.read().screen, scene_screen::none);  // no slots yet
  r.figure("HULK", false, 1, 1);
  EXPECT_EQ(r.read().screen, scene_screen::combat);
  r.data(data_slot_squares + 3, 72);
  EXPECT_EQ(r.read().screen, scene_screen::none);
  r.data(data_slot_squares + 3, 1);
  r.data_word(data_combat_block + 2, 0);
  EXPECT_EQ(r.read().screen, scene_screen::none);
}

TEST(SceneCombat, TheJsonHasNoSquareForTheParty) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.acting(r.figure("HULK", false, 12, 8));
  EXPECT_EQ(r.json().rfind(R"({"screen":"combat","disk":6,"area":25,)"
                           R"("zone":"WILDERNESS","turn":0,"waiting":true,)"
                           R"("nearest":null,)"
                           R"("combatants":[{"name":"HULK","side":"party",)"
                           R"("player":true,"standing":true,"status":"okay",)"
                           R"("x":12,"y":8,"width":1,"height":1,"hp":20,)"
                           R"("ac":4}],"window":{"x":10,"y":5,"rows":[[)",
                           0),
            0U)
      << r.json();
}

TEST(SceneCombat, TheRangeIsTheLongerDistanceAndHalfTheShorter) {
  EXPECT_EQ(scene_range(10, 10, 10, 10), 0);
  EXPECT_EQ(scene_range(10, 10, 13, 10), 3);
  EXPECT_EQ(scene_range(10, 10, 10, 6), 4);
  EXPECT_EQ(scene_range(10, 10, 13, 13), 4);
  EXPECT_EQ(scene_range(10, 10, 6, 13), 5);
  EXPECT_EQ(scene_range(13, 12, 10, 10), 4);
}

TEST(SceneCombat, EachFigureHasItsRangeFromTheActingOne) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  const unsigned hulk = r.figure("HULK", false, 12, 8);
  r.figure("MULE", false, 13, 8);
  r.figure("ORC", true, 14, 5);
  scene s = r.read();
  EXPECT_EQ(s.combatants[1].range, -1) << "no one's turn";
  EXPECT_EQ(s.nearest, -1);

  r.acting(hulk);
  s = r.read();
  EXPECT_EQ(s.combatants[0].range, -1) << "the acting figure itself";
  EXPECT_EQ(s.combatants[1].range, 1);
  EXPECT_EQ(s.combatants[2].range, 4);
  EXPECT_NE(r.json().find(R"("x":14,"y":5,"width":1,"height":1,"range":4})"),
            std::string::npos)
      << r.json();
}

TEST(SceneCombat, ALargeFigureIsMeasuredToItsNearestSquare) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.acting(r.figure("HULK", false, 12, 8));
  r.figure("DRAGON", true, 14, 8, 4);  // two by two, from (14, 8)
  r.figure("OGRE", true, 10, 9, 2);    // one wide, two tall, from (10, 9)
  const scene s = r.read();
  EXPECT_EQ(s.combatants[1].range, 2);
  EXPECT_EQ(s.combatants[2].range, 2);
}

TEST(SceneCombat, NearestIsTheClosestStandingEnemy) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.acting(r.figure("HULK", false, 12, 8));
  r.figure("MULE", false, 12, 9);
  r.figure("ORC", true, 16, 8);
  r.figure("GOBLIN", true, 14, 10);
  r.figure("KOBOLD", true, 12, 11);
  const scene s = r.read();
  // The goblin and the kobold are both three away; the first listed wins.
  EXPECT_EQ(s.combatants[3].range, 3);
  EXPECT_EQ(s.combatants[4].range, 3);
  EXPECT_EQ(s.nearest, 3);
  EXPECT_NE(r.json().find(R"("waiting":true,"nearest":3,)"), std::string::npos)
      << r.json();
}

TEST(SceneCombat, OnAMonstersTurnTheNearestIsAPartyMember) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.figure("HULK", false, 12, 8);
  r.figure("MULE", false, 15, 8);
  const unsigned orc = r.figure("ORC", true, 16, 7);
  r.figure("GOBLIN", true, 16, 8);
  r.acting(orc);
  const scene s = r.read();
  EXPECT_EQ(s.combatants[3].range, 1) << "its ally is nearer";
  EXPECT_EQ(s.nearest, 1);
}

TEST(SceneCombat, NoEnemyListedIsNoNearest) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.acting(r.figure("HULK", false, 12, 8));
  r.figure("ORC", true, 40, 20);  // outside the window
  EXPECT_EQ(r.read().nearest, -1);
}

TEST(SceneCombat, TheAimIsAbsentWhileNothingWatchesIt) {
  const rig r;
  r.load_baseline();
  r.combat(10, 5);
  r.figure("HULK", false, 12, 8);
  EXPECT_FALSE(r.read().aim_kept);
  EXPECT_EQ(r.json().find("aim"), std::string::npos) << r.json();
}

// --- The JSON ---------------------------------------------------------------

TEST(SceneJson, IsAskedForWithAnEmptyBufferThenFilled) {
  const rig r;
  r.load_baseline();
  r.interior(5, 5, north);
  scene now{};
  ASSERT_EQ(read_scene(*r.box, now), scene_trouble::none);
  const std::size_t length = scene_json(now, {});
  ASSERT_GT(length, 0U);

  std::string exact(length + 1, '#');
  EXPECT_EQ(scene_json(now, exact), length);
  EXPECT_EQ(exact[length], '\0');
  EXPECT_EQ(exact.front(), '{');
  EXPECT_EQ(exact[length - 1], '}');

  // A buffer one short is filled as far as it goes and gets no NUL.
  std::string short_by_one(length, '#');
  EXPECT_EQ(scene_json(now, short_by_one), length);
  EXPECT_EQ(short_by_one.find('\0'), std::string::npos);
}

TEST(SceneJson, StartsWithTheFieldsBothScreensHave) {
  const rig r;
  r.load_baseline();
  r.interior(7, 5, east);
  EXPECT_EQ(r.json().rfind(R"({"screen":"explore","disk":3,"area":0,)"
                           R"("zone":"NEW PHLAN","x":7,"y":5,)"
                           R"("facing":"east",)",
                           0),
            0U)
      << r.json();
}

}  // namespace
}  // namespace amberfolio::machine
