// SPDX-License-Identifier: AGPL-3.0-only
//
// The exploration sidecar (automap_store.h, M5-E2c #173).
//
// Two halves, and neither of them needs a program. The *format* is
// core's — `automap_state::write_sidecar`/`read_sidecar` — and is
// asserted against a table a test filled by hand. The *store* is this
// object, and is asserted by handing it a machine with a memory
// filesystem and the file events the DOS layer would have reported, then
// looking at what is in the filesystem afterwards.
//
// The paths and the file layout are restated here rather than read out of
// the code under test, which is `seam_automap_test.cpp`'s rule and the
// reason for it: a test that took its layout from the code it checks
// would be agreeing with itself. Every byte here is this file's own
// (PLAN.md §6).

#include "amberfolio/host/automap_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

using machine::automap_map_kind;
using machine::automap_marker;
using machine::automap_max_markers;
using machine::automap_record;
using machine::automap_state;
using machine::dos_path;
using machine::file_action;
using machine::file_event;
using machine::vfs_error;

/// The names this store owns, and the program's own slot files it
/// watches for. Spelled out, because they are the interface a player's
/// directory sees.
constexpr std::string_view working_path = "SAVE\\AFMAP.DAT";
constexpr std::string_view slot_a_path = "SAVE\\AFMAPA.DAT";
constexpr std::string_view slot_b_path = "SAVE\\AFMAPB.DAT";
constexpr std::string_view game_slot_a = "SAVE\\SAVGAMA.DAT";
constexpr std::string_view game_slot_b = "SAVE\\SAVGAMB.DAT";

[[nodiscard]] dos_path path_of(std::string_view text) {
  const machine::vfs_result<dos_path> where =
      machine::canonicalize_host_path({text.data(), text.size()});
  EXPECT_TRUE(where.ok()) << text;
  return where.value;
}

/// One file event, as the DOS layer reports one. `moved` is whether
/// bytes went through the handle, which is meaningful for a close and is
/// what tells a load from the load menu's look at every slot in turn.
[[nodiscard]] file_event event_of(file_action what, std::string_view path,
                                  bool moved = true) {
  file_event event{};
  event.what = what;
  event.path = path_of(path);
  event.handle = 5;
  event.error = vfs_error::none;
  event.read_through = moved;
  event.written_through = moved;
  return event;
}

struct rig {
  rig()
      : files(std::make_unique<machine::memory_filesystem>()),
        box(std::make_unique<machine::machine>(machine::memory_layout::pc)) {
    box->set_filesystem(*files);
  }

  [[nodiscard]] automap_state& maps() const noexcept { return box->automap(); }

  /// Explore one cell of one map, the way the panel's reveal would.
  void explore(std::uint8_t disk, std::uint8_t area, std::uint8_t geo,
               unsigned x, unsigned y) const {
    automap_record& map = maps().record_for(disk, area, geo);
    maps().reveal(map, x, y);
  }

  /// Whether a path is in the filesystem at all.
  [[nodiscard]] bool has(std::string_view path) const {
    return files->exists(path_of(path));
  }

  /// The whole of a file, as bytes.
  [[nodiscard]] std::vector<std::uint8_t> bytes_of(std::string_view path) {
    std::vector<std::uint8_t> out;
    const machine::vfs_result<machine::file_handle> file =
        files->open(path_of(path), machine::open_mode::read_only);
    EXPECT_TRUE(file.ok());
    std::array<std::uint8_t, automap_store_capacity> buffer{};
    const machine::vfs_result<std::size_t> got =
        files->read(file.value, buffer);
    EXPECT_TRUE(got.ok());
    out.assign(buffer.begin(),
               buffer.begin() + static_cast<std::ptrdiff_t>(got.value));
    EXPECT_EQ(files->close(file.value), vfs_error::none);
    return out;
  }

  /// Put a file there that this store did not write.
  void put(std::string_view path, std::span<const std::uint8_t> data) {
    (void)files->mkdir(path_of(path).parent());
    const machine::vfs_result<machine::file_handle> file =
        files->create(path_of(path));
    ASSERT_TRUE(file.ok());
    const machine::vfs_result<std::size_t> wrote =
        files->write(file.value, data);
    ASSERT_TRUE(wrote.ok());
    ASSERT_EQ(files->close(file.value), vfs_error::none);
  }

  /// On the heap, both of them: the filesystem's arena alone is eight
  /// megabytes, which is two orders of magnitude past a thread's stack.
  std::unique_ptr<machine::memory_filesystem> files;
  std::unique_ptr<machine::machine> box;
  automap_store store;
};

// ---------------------------------------------------------------------------
// The format, on its own
// ---------------------------------------------------------------------------

TEST(AutomapSidecar, ATableSurvivesTheRoundTrip) {
  automap_state before;
  automap_record& city = before.record_for(3, 0, 0);
  before.reveal(city, 8, 11);
  before.reveal(city, 8, 10);
  before.mark(city, 8, 11, automap_marker::entrance);
  automap_record& keep = before.record_for(8, 16, 30);
  before.reveal(keep, 1, 1);
  before.mark(keep, 2, 2, automap_marker::exit);

  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = before.write_sidecar(bytes);
  ASSERT_EQ(size, before.sidecar_bytes());
  ASSERT_GT(size, 0u);

  automap_state after;
  ASSERT_TRUE(after.read_sidecar({bytes.data(), size}));
  EXPECT_EQ(after.records_used(), 2u);

  const automap_record* city_back = after.find(3, 0, 0);
  ASSERT_NE(city_back, nullptr);
  EXPECT_TRUE(automap_state::seen(*city_back, 8, 11));
  EXPECT_TRUE(automap_state::seen(*city_back, 8, 10));
  EXPECT_FALSE(automap_state::seen(*city_back, 0, 0));
  EXPECT_EQ(automap_state::marker_at(*city_back, 8, 11),
            automap_marker::entrance);

  const automap_record* keep_back = after.find(8, 16, 30);
  ASSERT_NE(keep_back, nullptr);
  EXPECT_TRUE(automap_state::seen(*keep_back, 1, 1));
  EXPECT_EQ(automap_state::marker_at(*keep_back, 2, 2), automap_marker::exit);

  // And the third byte of the identity is in it: a grid swapped under a
  // fixed area is a different record and must come back as one.
  EXPECT_EQ(after.find(8, 16, 0), nullptr);
}

TEST(AutomapSidecar, ItSaysWhatItIsAndRefusesWhatItIsNot) {
  automap_state state;
  state.reveal(state.record_for(3, 0, 0), 1, 1);
  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = state.write_sidecar(bytes);
  ASSERT_GT(size, 0u);

  // Three bytes of name and a version, which is what a reader checks.
  EXPECT_EQ(bytes[0], 'A');
  EXPECT_EQ(bytes[1], 'F');
  EXPECT_EQ(bytes[2], 'M');
  EXPECT_EQ(bytes[3], machine::automap_sidecar_version);

  automap_state other;
  other.reveal(other.record_for(9, 9, 9), 5, 5);

  // A version it does not know, and what was already explored is
  // untouched: a refusal must not be a way of losing a map.
  std::array<std::uint8_t, automap_store_capacity> wrong = bytes;
  wrong[3] = 99;
  EXPECT_FALSE(other.read_sidecar({wrong.data(), size}));
  EXPECT_NE(other.find(9, 9, 9), nullptr);

  // Not a sidecar at all.
  const std::array<std::uint8_t, 8> rubbish{1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_FALSE(other.read_sidecar(rubbish));
  EXPECT_NE(other.find(9, 9, 9), nullptr);

  // Cut short. Half a table read as a whole one is a map with holes in it
  // that nobody could tell from unexplored ground.
  EXPECT_FALSE(other.read_sidecar({bytes.data(), size - 1}));
  EXPECT_NE(other.find(9, 9, 9), nullptr);
}

TEST(AutomapSidecar, AnOverlandRecordSurvivesTheRoundTrip) {
  automap_state before;
  automap_record& wilderness = before.record_for_overland(6, 0x19);
  before.reveal(wilderness, 3, 32);
  before.reveal(wilderness, 15, 35);
  // The same disk and area as an interior record whose geometry block is
  // zero: only the kind keeps them apart.
  before.reveal(before.record_for(6, 0x19, 0), 3, 12);

  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = before.write_sidecar(bytes);
  ASSERT_GT(size, 0u);

  automap_state after;
  ASSERT_TRUE(after.read_sidecar({bytes.data(), size}));
  EXPECT_EQ(after.records_used(), 2u);

  const automap_record* back = after.find_overland(6, 0x19);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->kind, automap_map_kind::overland);
  EXPECT_TRUE(automap_state::seen(*back, 3, 32));
  EXPECT_TRUE(automap_state::seen(*back, 15, 35))
      << "the far corner, which is past where a grid's bitmap ends";
  EXPECT_FALSE(automap_state::seen(*back, 3, 12));

  const automap_record* grid = after.find(6, 0x19, 0);
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->kind, automap_map_kind::grid);
  EXPECT_TRUE(automap_state::seen(*grid, 3, 12));
}

TEST(AutomapSidecar, AFileFromTheVersionBeforeTheOverlandStillOpens) {
  // Laid out from version 1's own documented shape (automap.h) rather
  // than written by anything here: the point is that a player's existing
  // `AFMAP.DAT` opens, and a test that produced it with today's writer
  // would only be agreeing with itself.
  constexpr std::size_t header = machine::automap_sidecar_header_bytes;
  constexpr std::size_t stride = machine::automap_sidecar_v1_record_bytes;
  std::vector<std::uint8_t> file(header + stride, 0);
  file[0] = 'A';
  file[1] = 'F';
  file[2] = 'M';
  file[3] = machine::automap_sidecar_first_version;
  file[4] = 1;  // one record
  file[5] = 0;
  file[6] = static_cast<std::uint8_t>(stride);
  file[7] = static_cast<std::uint8_t>(stride >> 8U);

  std::uint8_t* row = file.data() + header;
  row[0] = 3;  // disk
  row[1] = 0;  // area
  row[2] = 3;  // geometry block
  row[3] = 1;  // one mark
  // Cell (8, 11) of a sixteen-by-sixteen grid, bit `y * 16 + x`.
  constexpr unsigned cell = (11U * 16U) + 8U;
  row[4 + (cell / 8U)] = static_cast<std::uint8_t>(1U << (cell % 8U));
  row[4 + machine::automap_grid_seen_bytes] = 8;  // mark x
  row[4 + machine::automap_grid_seen_bytes + automap_max_markers] = 11;
  row[4 + machine::automap_grid_seen_bytes + (2 * automap_max_markers)] =
      static_cast<std::uint8_t>(automap_marker::entrance);

  automap_state state;
  ASSERT_TRUE(state.read_sidecar(file));
  EXPECT_EQ(state.records_used(), 1u);
  const automap_record* back = state.find(3, 0, 3);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->kind, automap_map_kind::grid) << "a version-1 record is one";
  EXPECT_TRUE(automap_state::seen(*back, 8, 11));
  EXPECT_FALSE(automap_state::seen(*back, 0, 0));
  EXPECT_EQ(automap_state::marker_at(*back, 8, 11), automap_marker::entrance);

  // And what is written back out is version 2, whatever was read in.
  std::array<std::uint8_t, automap_store_capacity> out{};
  const std::size_t size = state.write_sidecar(out);
  ASSERT_GT(size, 0u);
  EXPECT_EQ(out[3], machine::automap_sidecar_version);
}

TEST(AutomapSidecar, AVersionOneHeaderWithTodaysStrideIsRefused) {
  // The version and the record width have to agree, or a reader indexes
  // into the wrong rows and paints a map nobody has walked.
  automap_state state;
  state.reveal(state.record_for(3, 0, 0), 1, 1);
  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = state.write_sidecar(bytes);
  ASSERT_GT(size, 0u);
  bytes[3] = machine::automap_sidecar_first_version;

  automap_state other;
  other.reveal(other.record_for(9, 9, 9), 5, 5);
  EXPECT_FALSE(other.read_sidecar({bytes.data(), size}));
  EXPECT_NE(other.find(9, 9, 9), nullptr) << "and nothing was lost saying so";
}

TEST(AutomapSidecar, AnEmptyTableIsAFileAndNotNothing) {
  automap_state state;
  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = state.write_sidecar(bytes);
  EXPECT_EQ(size, 8u) << "the header alone";

  automap_state after;
  after.reveal(after.record_for(1, 1, 1), 0, 0);
  EXPECT_TRUE(after.read_sidecar({bytes.data(), size}));
  EXPECT_EQ(after.records_used(), 0u) << "and it replaces what was there";
}

TEST(AutomapSidecar, ABufferTooSmallIsRefusedRatherThanTruncated) {
  automap_state state;
  state.reveal(state.record_for(3, 0, 0), 1, 1);
  std::array<std::uint8_t, 16> tiny{};
  EXPECT_EQ(state.write_sidecar(tiny), 0u);
}

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

TEST(AutomapStore, OffItReadsNothingAndWritesNothing) {
  rig r;
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 4, 4);
  r.store.changed();
  r.store.saw(event_of(file_action::create, game_slot_a));
  r.store.saw(event_of(file_action::close, game_slot_a));

  EXPECT_FALSE(r.has(working_path));
  EXPECT_FALSE(r.has(slot_a_path));
  EXPECT_EQ(r.store.writes(), 0u);
  EXPECT_EQ(r.store.reads(), 0u);
}

TEST(AutomapStore, OnItWritesTheWorkingTableWhenTheMapMoves) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  EXPECT_EQ(r.store.reads(), 0u) << "there is nothing there to read yet";

  r.explore(3, 0, 0, 4, 4);
  r.store.changed();

  ASSERT_TRUE(r.has(working_path));
  EXPECT_EQ(r.store.writes(), 1u);
  EXPECT_EQ(r.store.trouble(), automap_trouble::none);

  const std::vector<std::uint8_t> written = r.bytes_of(working_path);
  automap_state read_back;
  ASSERT_TRUE(read_back.read_sidecar(written));
  const automap_record* map = read_back.find(3, 0, 0);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 4, 4));
}

TEST(AutomapStore, AWorkingTableComesBackWhenTheNextMachineAttaches) {
  rig first;
  first.store.enable(true);
  first.store.attach(*first.box);
  first.explore(3, 0, 0, 6, 6);
  first.store.changed();
  const std::vector<std::uint8_t> written = first.bytes_of(working_path);

  rig second;
  second.put(working_path, written);
  second.store.enable(true);
  second.store.attach(*second.box);

  EXPECT_EQ(second.store.reads(), 1u);
  const automap_record* map = second.maps().find(3, 0, 0);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 6, 6));
}

TEST(AutomapStore, ASavedSlotGetsItsOwnSnapshot) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 2, 3);

  // The program makes the slot file and closes it. Created means saved.
  r.store.saw(event_of(file_action::create, game_slot_a));
  r.store.saw(event_of(file_action::close, game_slot_a));

  EXPECT_EQ(r.store.slot(), 'A');
  ASSERT_TRUE(r.has(slot_a_path)) << "the snapshot";
  ASSERT_TRUE(r.has(working_path)) << "and the working table follows it";

  automap_state snapshot;
  ASSERT_TRUE(snapshot.read_sidecar(r.bytes_of(slot_a_path)));
  const automap_record* map = snapshot.find(3, 0, 0);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 2, 3));
}

TEST(AutomapStore, LoadingASlotReplacesTheTableWithThatSlotsOwn) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);

  // Slot A: one cell of one map, saved.
  r.explore(3, 0, 0, 2, 3);
  r.store.saw(event_of(file_action::create, game_slot_a));
  r.store.saw(event_of(file_action::close, game_slot_a));

  // Slot B: a different playthrough, somewhere else entirely.
  r.maps().clear();
  r.explore(8, 29, 4, 12, 12);
  r.store.saw(event_of(file_action::create, game_slot_b));
  r.store.saw(event_of(file_action::close, game_slot_b));
  ASSERT_TRUE(r.has(slot_a_path));
  ASSERT_TRUE(r.has(slot_b_path)) << "two slots, two snapshots";

  // Now the player loads A back. Found, not created, is a load.
  r.store.saw(event_of(file_action::open, game_slot_a));
  r.store.saw(event_of(file_action::close, game_slot_a));

  const automap_record* city = r.maps().find(3, 0, 0);
  ASSERT_NE(city, nullptr);
  EXPECT_TRUE(automap_state::seen(*city, 2, 3));
  EXPECT_EQ(r.maps().find(8, 29, 4), nullptr)
      << "B's streets are not in A's map";
}

TEST(AutomapStore, ASlotWithNoSnapshotComesBackEmpty) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 1, 1);

  // A save made before this was ever switched on: the game's slot file
  // exists, ours does not. The party now in the machine is that slot's
  // party, and it has never walked anywhere this table knows about.
  r.store.saw(event_of(file_action::open, game_slot_b));
  r.store.saw(event_of(file_action::close, game_slot_b));

  EXPECT_EQ(r.maps().records_used(), 0u)
      << "the previous playthrough's streets are not this one's";
  EXPECT_EQ(r.store.reads(), 0u);
  EXPECT_EQ(r.store.trouble(), automap_trouble::none);
}

TEST(AutomapStore, LoadingASlotDoesNotCloseThePanelThePlayerHasOpen) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 1, 1);
  r.maps().set_panel_open(true);

  r.store.saw(event_of(file_action::open, game_slot_b));
  r.store.saw(event_of(file_action::close, game_slot_b));

  EXPECT_EQ(r.maps().records_used(), 0u) << "the records went";
  EXPECT_TRUE(r.maps().panel_open())
      << "and the panel did not: which map is remembered is not whether "
         "the player asked to see one";
}

TEST(AutomapStore, TheLoadMenuLookingAtEverySlotIsNotNineLoads) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);

  // Slot B has a snapshot; slot A does not.
  r.explore(8, 29, 4, 12, 12);
  r.store.saw(event_of(file_action::create, game_slot_b));
  r.store.saw(event_of(file_action::close, game_slot_b));
  r.maps().clear();
  r.explore(3, 0, 0, 7, 7);

  // The load menu opens every slot in the directory in turn to find out
  // which exist, and gives each back without reading a byte. None of
  // those is a load.
  r.store.saw(event_of(file_action::open, game_slot_a, false));
  r.store.saw(event_of(file_action::close, game_slot_a, false));
  r.store.saw(event_of(file_action::open, game_slot_b, false));
  r.store.saw(event_of(file_action::close, game_slot_b, false));

  EXPECT_EQ(r.store.slot(), 'B') << "the save, and neither of the looks";
  const automap_record* here = r.maps().find(3, 0, 0);
  ASSERT_NE(here, nullptr) << "and the working table is untouched";
  EXPECT_TRUE(automap_state::seen(*here, 7, 7));
  EXPECT_EQ(r.maps().find(8, 29, 4), nullptr) << "B's map was not pulled in";
}

TEST(AutomapStore, ItOnlyWatchesTheProgramsSaveSlots) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 1, 1);

  // Every other file the program touches while it saves — the character
  // files beside the slot, the archives it reads all run long — must not
  // be mistaken for one.
  r.store.saw(event_of(file_action::create, "SAVE\\CHRDATA1.SAV"));
  r.store.saw(event_of(file_action::close, "SAVE\\CHRDATA1.SAV"));
  r.store.saw(event_of(file_action::open, "GEO3.DAX"));
  r.store.saw(event_of(file_action::close, "GEO3.DAX"));
  r.store.saw(event_of(file_action::open, "SAVE\\SAVGAM.DAT"));
  r.store.saw(event_of(file_action::close, "SAVE\\SAVGAM.DAT"));

  EXPECT_EQ(r.store.slot(), 0);
  EXPECT_EQ(r.store.writes(), 0u);
  EXPECT_FALSE(r.has(working_path));
}

TEST(AutomapStore, AFailedOpenIsNotASlotEvent) {
  rig r;
  r.store.enable(true);
  r.store.attach(*r.box);
  r.explore(3, 0, 0, 1, 1);

  // The program asking whether a slot exists is exactly how the load
  // menu builds its list, and it is not a load.
  file_event missing = event_of(file_action::open, game_slot_a);
  missing.error = vfs_error::file_not_found;
  r.store.saw(missing);

  EXPECT_EQ(r.store.slot(), 0);
  EXPECT_EQ(r.store.reads(), 0u);
}

TEST(AutomapStore, SomethingElseUnderOurNameIsRefusedAndSaidOutLoud) {
  rig r;
  const std::array<std::uint8_t, 12> impostor{'N', 'O', 'T', 'M', 'I', 'N',
                                              'E', 0,   0,   0,   0,   0};
  r.put(working_path, impostor);
  r.store.enable(true);
  r.store.attach(*r.box);

  EXPECT_EQ(r.store.reads(), 0u);
  EXPECT_EQ(r.store.trouble(), automap_trouble::not_a_sidecar);
  EXPECT_EQ(r.maps().records_used(), 0u);
}

}  // namespace
}  // namespace amberfolio::host
