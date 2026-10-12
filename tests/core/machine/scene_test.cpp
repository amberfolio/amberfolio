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
