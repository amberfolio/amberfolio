// SPDX-License-Identifier: AGPL-3.0-only
//
// The overlay residency tracker (overlay.h, M4-F3 #97): what a DOS read
// leaves in the table, what a later read into the same memory does to
// it, and what a seam's module reference has to match to be called
// resident.
//
// Two layers. The first drives `overlay_tracker` directly, because the
// replacement and eviction rules are the tracker's own and are easiest
// to state against it. The second drives a real `INT 21h AH=3Fh` through
// the machine, the way dos_test.cpp drives every function it asserts, so
// that what the tracker records is what the read handler actually told
// it — the file, the position, DS:DX, the count and the digest — and not
// a test's idea of those.
//
// Every byte here is this file's own (PLAN.md §6).

#include "amberfolio/machine/overlay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] dos_path path_of(std::string_view raw) {
  const vfs_result<dos_path> resolved =
      canonicalize(dos_path{}, std::span<const char>(raw.data(), raw.size()));
  EXPECT_TRUE(resolved.ok()) << raw;
  return resolved.value;
}

[[nodiscard]] sha256_digest digest_of(
    std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> data(bytes);
  return sha256(data);
}

// --- The table's own rules ----------------------------------------------

TEST(OverlayTracker, RecordsARead) {
  overlay_tracker tracker;
  const sha256_digest digest = digest_of({1, 2, 3});
  tracker.note_read(path_of("\\OVL.BIN"), 0x100, 0x2000, 0x0010, 0x40, digest);

  ASSERT_EQ(tracker.count(), 1u);
  const overlay_load& load = tracker.at(0);
  EXPECT_EQ(load.file_offset, 0x100u);
  EXPECT_EQ(load.length, 0x40u);
  EXPECT_EQ(load.segment, 0x2000);
  EXPECT_EQ(load.offset, 0x0010);
  EXPECT_EQ(load.digest, digest);
  EXPECT_EQ(load.first(), cpu::physical_address(0x2000, 0x0010));
  EXPECT_EQ(load.last(), cpu::physical_address(0x2000, 0x0010) + 0x3F);
  EXPECT_EQ(tracker.generation(), 1u);
}

TEST(OverlayTracker, IgnoresAZeroLengthRead) {
  overlay_tracker tracker;
  tracker.note_read(path_of("\\OVL.BIN"), 0, 0x2000, 0, 0, sha256_digest{});
  EXPECT_EQ(tracker.count(), 0u);
  EXPECT_EQ(tracker.generation(), 0u);
}

TEST(OverlayTracker, AnswersResidentForAModuleThatMatchesTheFacts) {
  overlay_tracker tracker;
  tracker.note_read(path_of("\\OVL.BIN"), 0x100, 0x2000, 0x0010, 0x40,
                    digest_of({9}));

  const seam_module exact{
      .file = "OVL.BIN", .file_offset = 0x100, .length = 0x40};
  ASSERT_NE(tracker.resident(exact), nullptr);
  EXPECT_EQ(tracker.resident(exact)->segment, 0x2000);

  const seam_module other_file{
      .file = "DATA.BIN", .file_offset = 0x100, .length = 0x40};
  EXPECT_EQ(tracker.resident(other_file), nullptr);
  const seam_module other_offset{
      .file = "OVL.BIN", .file_offset = 0x140, .length = 0x40};
  EXPECT_EQ(tracker.resident(other_offset), nullptr);
  const seam_module other_length{
      .file = "OVL.BIN", .file_offset = 0x100, .length = 0x41};
  EXPECT_EQ(tracker.resident(other_length), nullptr);

  // The resident image is never in the table; a caller asks the engine
  // about it, not the tracker.
  EXPECT_EQ(tracker.resident(resident_image), nullptr);
}

TEST(OverlayTracker, ADigestInTheModuleIsCheckedWhenGiven) {
  overlay_tracker tracker;
  const sha256_digest digest = digest_of({4, 5, 6});
  tracker.note_read(path_of("\\OVL.BIN"), 0, 0x2000, 0, 3, digest);

  std::array<char, sha256_digest::text_length + 1> hex{};
  ASSERT_EQ(format_hex(digest, hex), sha256_digest::text_length);
  const seam_module right{.file = "OVL.BIN",
                          .file_offset = 0,
                          .length = 3,
                          .digest = std::string_view(hex.data())};
  EXPECT_NE(tracker.resident(right), nullptr);

  const seam_module wrong{
      .file = "OVL.BIN",
      .file_offset = 0,
      .length = 3,
      .digest =
          "0000000000000000000000000000000000000000000000000000000000000000"};
  EXPECT_EQ(tracker.resident(wrong), nullptr)
      << "the bytes that arrived are not the bytes the seam describes";
}

TEST(OverlayTracker, AReadIntoTheSameMemoryReplacesWhatWasThere) {
  overlay_tracker tracker;
  tracker.note_read(path_of("\\OVL.BIN"), 0x100, 0x2000, 0x0000, 0x100,
                    digest_of({1}));
  // A different module, landing halfway into the first one's range.
  tracker.note_read(path_of("\\OVL.BIN"), 0x900, 0x2000, 0x0080, 0x20,
                    digest_of({2}));

  ASSERT_EQ(tracker.count(), 1u);
  EXPECT_EQ(tracker.at(0).file_offset, 0x900u);
  EXPECT_EQ(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x100, .length = 0x100}),
            nullptr)
      << "the first module is gone: its memory holds something else now";
  EXPECT_NE(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x900, .length = 0x20}),
            nullptr);
}

TEST(OverlayTracker, ReadsIntoDistinctMemoryCoexist) {
  overlay_tracker tracker;
  tracker.note_read(path_of("\\OVL.BIN"), 0x100, 0x2000, 0x0000, 0x100,
                    digest_of({1}));
  tracker.note_read(path_of("\\OVL.BIN"), 0x900, 0x3000, 0x0000, 0x20,
                    digest_of({2}));
  EXPECT_EQ(tracker.count(), 2u);
  EXPECT_NE(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x100, .length = 0x100}),
            nullptr);
  EXPECT_NE(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x900, .length = 0x20}),
            nullptr);
}

TEST(OverlayTracker, AFullTableDropsItsOldestEntry) {
  overlay_tracker tracker;
  for (std::uint32_t i = 0; i < overlay_tracker::max_modules; ++i) {
    // Each into its own paragraph-aligned range, so none overlaps.
    tracker.note_read(path_of("\\OVL.BIN"), i * 0x10,
                      static_cast<std::uint16_t>(0x2000 + i * 0x10), 0, 0x10,
                      digest_of({static_cast<std::uint8_t>(i)}));
  }
  ASSERT_EQ(tracker.count(), overlay_tracker::max_modules);

  tracker.note_read(path_of("\\OVL.BIN"), 0x9000, 0x7000, 0, 0x10,
                    digest_of({0xFF}));
  EXPECT_EQ(tracker.count(), overlay_tracker::max_modules);
  EXPECT_EQ(
      tracker.resident({.file = "OVL.BIN", .file_offset = 0, .length = 0x10}),
      nullptr)
      << "the oldest went";
  EXPECT_NE(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x10, .length = 0x10}),
            nullptr)
      << "the second oldest stayed";
  EXPECT_NE(tracker.resident(
                {.file = "OVL.BIN", .file_offset = 0x9000, .length = 0x10}),
            nullptr);
}

TEST(OverlayTracker, ClearForgetsEverything) {
  overlay_tracker tracker;
  tracker.note_read(path_of("\\OVL.BIN"), 0, 0x2000, 0, 0x10, digest_of({1}));
  tracker.clear();
  EXPECT_EQ(tracker.count(), 0u);
  EXPECT_EQ(tracker.generation(), 0u);
}

// --- Through the DOS read handler -----------------------------------------

constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t stack_top = 0x1000;
constexpr std::uint16_t path_area = 0x0080;
constexpr std::uint16_t data_area = 0x0200;

/// The rig dos_test.cpp uses, cut down to the one call under test.
struct rig {
  rig()
      : fs(std::make_unique<memory_filesystem>()),
        box(std::make_unique<machine>(memory_layout::pc, &log)) {
    box->set_filesystem(*fs);
    install_dos_services(box->services());
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  void stage(std::string_view raw, std::span<const std::uint8_t> bytes) const {
    const auto made = fs->create(path_of(raw));
    ASSERT_TRUE(made.ok());
    const auto wrote = fs->write(made.value, bytes);
    ASSERT_TRUE(wrote.ok());
    ASSERT_EQ(wrote.value, bytes.size());
    ASSERT_EQ(fs->close(made.value), vfs_error::none);
  }

  void write_asciz(std::uint16_t at, std::string_view text) const {
    std::uint32_t p = cpu::physical_address(code_segment, at);
    for (const char c : text) {
      box->memory().ram()[p++] = static_cast<std::uint8_t>(c);
    }
    box->memory().ram()[p] = 0;
  }

  /// `INT 21h ; HLT` with AX/BX/CX/DX as given, run to the HLT.
  void call(std::uint16_t ax, std::uint16_t bx = 0, std::uint16_t cx = 0,
            std::uint16_t dx = 0) const {
    std::uint32_t p = cpu::physical_address(code_segment, 0);
    constexpr std::array<std::uint8_t, 3> int21_hlt{0xCD, 0x21, 0xF4};
    for (const std::uint8_t byte : int21_hlt) {
      box->memory().ram()[p++] = byte;
    }
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = code_segment;
    r[cpu::sreg::ss] = code_segment;
    r[cpu::sreg::ds] = code_segment;
    r[cpu::reg16::sp] = stack_top;
    r.ip = 0;
    r[cpu::reg16::ax] = ax;
    r[cpu::reg16::bx] = bx;
    r[cpu::reg16::cx] = cx;
    r[cpu::reg16::dx] = dx;
    for (unsigned i = 0;
         i < 4000 && !box->processor().halted() && !box->stopped(); ++i) {
      box->step();
    }
    ASSERT_TRUE(box->processor().halted());
  }

  test::recording_diagnostics log;
  std::unique_ptr<memory_filesystem> fs;
  std::unique_ptr<machine> box;
};

TEST(OverlayTrackerThroughDos, RecordsWhatAReadActuallyDid) {
  const rig r;
  const std::array<std::uint8_t, 48> module{
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
      0xDD, 0xEE, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x20, 0x30, 0x40, 0x50,
      0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x12, 0x34};
  r.stage("\\OVL.BIN", module);
  r.write_asciz(path_area, "\\OVL.BIN");

  // Open, seek to 16, read 20 bytes into DS:0200h.
  r.call(0x3D00, 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(0x4200, handle, 0, 16);
  r.call(0x3F00, handle, 20, data_area);
  ASSERT_EQ(r.regs()[cpu::reg16::ax], 20u);

  ASSERT_EQ(r.pc().overlays().count(), 1u);
  const overlay_load& load = r.pc().overlays().at(0);
  EXPECT_EQ(load.file_offset, 16u) << "the position before the read";
  EXPECT_EQ(load.length, 20u) << "the bytes that arrived, not CX";
  EXPECT_EQ(load.segment, code_segment);
  EXPECT_EQ(load.offset, data_area);
  EXPECT_EQ(load.digest,
            sha256(std::span<const std::uint8_t>(module.data() + 16, 20)))
      << "the digest is of exactly the bytes the program got";
  EXPECT_EQ(load.file.depth(), 1u);

  // And the module reference a seam would write for it is resident.
  EXPECT_NE(r.pc().overlays().resident(
                {.file = "OVL.BIN", .file_offset = 16, .length = 20}),
            nullptr);
}

TEST(OverlayTrackerThroughDos, AShortReadRecordsWhatArrivedNotWhatWasAsked) {
  const rig r;
  const std::array<std::uint8_t, 8> tiny{1, 2, 3, 4, 5, 6, 7, 8};
  r.stage("\\OVL.BIN", tiny);
  r.write_asciz(path_area, "\\OVL.BIN");

  r.call(0x3D00, 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(0x3F00, handle, 0x40, data_area);
  ASSERT_EQ(r.regs()[cpu::reg16::ax], 8u);

  ASSERT_EQ(r.pc().overlays().count(), 1u);
  EXPECT_EQ(r.pc().overlays().at(0).length, 8u);
}

TEST(OverlayTrackerThroughDos, AReadAtEndOfFileRecordsNothing) {
  const rig r;
  const std::array<std::uint8_t, 4> tiny{1, 2, 3, 4};
  r.stage("\\OVL.BIN", tiny);
  r.write_asciz(path_area, "\\OVL.BIN");

  r.call(0x3D00, 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(0x3F00, handle, 4, data_area);
  ASSERT_EQ(r.regs()[cpu::reg16::ax], 4u);
  r.call(0x3F00, handle, 4, data_area);
  ASSERT_EQ(r.regs()[cpu::reg16::ax], 0u) << "at end of file";

  EXPECT_EQ(r.pc().overlays().count(), 1u) << "nothing new was loaded";
  EXPECT_EQ(r.pc().overlays().generation(), 1u);
}

TEST(OverlayTrackerThroughDos, ResetForgetsTheTable) {
  const rig r;
  const std::array<std::uint8_t, 4> tiny{1, 2, 3, 4};
  r.stage("\\OVL.BIN", tiny);
  r.write_asciz(path_area, "\\OVL.BIN");
  r.call(0x3D00, 0, 0, path_area);
  r.call(0x3F00, r.regs()[cpu::reg16::ax], 4, data_area);
  ASSERT_EQ(r.pc().overlays().count(), 1u);

  r.pc().reset();
  EXPECT_EQ(r.pc().overlays().count(), 0u);
}

}  // namespace
}  // namespace amberfolio::machine
