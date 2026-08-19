// SPDX-License-Identifier: AGPL-3.0-only
//
// The MZ loader: header decode/validation edge cases, relocation
// application (including the wraparound case loader.h's top comment
// explains), the PSP bytes, entry register state, exit propagation
// through INT 20h, and the exit criterion — a self-written multi-segment
// EXE with relocations that runs to its computed answer through the
// machine.
//
// Every EXE fixture below is built by `build_exe()`, which assembles the
// MZ container format from a header, a relocation table and an image —
// none of it any byte of any real game (PLAN.md §6, the clean-content
// rule applies to test data exactly as it does to everything else). The
// programs inside those images are written by hand from the encoding,
// the same discipline `tests/programs/assembler.h` documents for the
// CPU's own fixtures.

#include "amberfolio/machine/loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/memory_vfs.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

// --- Building an MZ file byte for byte ----------------------------------

/// Where `build_exe()` always puts the relocation table: immediately
/// after the fixed 28-byte header, which is where an assembler with
/// nothing else to put there naturally leaves it.
constexpr std::uint16_t reloc_table_file_offset = mz::encoded_size;

void put16(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint16_t value) {
  bytes[at] = static_cast<std::uint8_t>(value);
  bytes[at + 1] = static_cast<std::uint8_t>(value >> 8u);
}

/// One relocation table entry: offset, then segment — the file's own
/// field order, matched here so a fixture reads the way the format does.
struct reloc_entry {
  std::uint16_t offset{};
  std::uint16_t segment{};
};

struct exe_spec {
  std::uint16_t initial_cs{};
  std::uint16_t initial_ip{};
  std::uint16_t initial_ss{};
  std::uint16_t initial_sp{};
  std::uint16_t min_alloc{};
  std::vector<reloc_entry> relocations;
  std::vector<std::uint8_t> image;
};

/// Assemble a complete, well-formed MZ file from `spec`: the fixed
/// header, the relocation table right after it, and the image
/// immediately after that — every length field computed from the
/// pieces actually given, including the last-page-size rule, so a test
/// that wants a malformed field starts from here and corrupts exactly
/// the one byte it means to.
[[nodiscard]] std::vector<std::uint8_t> build_exe(const exe_spec& spec) {
  const auto reloc_count = static_cast<std::uint16_t>(spec.relocations.size());
  const std::uint32_t reloc_table_bytes =
      static_cast<std::uint32_t>(reloc_count) * mz::relocation_entry_size;
  const std::uint32_t header_bytes_needed =
      reloc_table_file_offset + reloc_table_bytes;
  const auto header_paragraphs = static_cast<std::uint16_t>(
      (header_bytes_needed + paragraph_size - 1) / paragraph_size);
  const std::uint32_t header_size =
      static_cast<std::uint32_t>(header_paragraphs) * paragraph_size;

  const std::uint32_t total_length =
      header_size + static_cast<std::uint32_t>(spec.image.size());
  const auto last_page_size =
      static_cast<std::uint16_t>(total_length % mz::page_size);
  const auto page_count = static_cast<std::uint16_t>(
      total_length / mz::page_size + (last_page_size != 0 ? 1 : 0));

  std::vector<std::uint8_t> file(header_size + spec.image.size(), 0);

  file[0] = 'M';
  file[1] = 'Z';
  put16(file, 0x02, last_page_size);
  put16(file, 0x04, page_count);
  put16(file, 0x06, reloc_count);
  put16(file, 0x08, header_paragraphs);
  put16(file, 0x0A, spec.min_alloc);
  put16(file, 0x0C, 0xFFFF);  // MAXALLOC: never consulted (loader.h).
  put16(file, 0x0E, spec.initial_ss);
  put16(file, 0x10, spec.initial_sp);
  put16(file, 0x12, 0);  // checksum: no reader of it exists.
  put16(file, 0x14, spec.initial_ip);
  put16(file, 0x16, spec.initial_cs);
  put16(file, 0x18, reloc_table_file_offset);
  put16(file, 0x1A, 0);  // overlay number: not read by anything here.

  for (std::size_t i = 0; i < spec.relocations.size(); ++i) {
    const std::size_t at =
        reloc_table_file_offset + i * mz::relocation_entry_size;
    put16(file, at, spec.relocations[i].offset);
    put16(file, at + 2, spec.relocations[i].segment);
  }

  for (std::size_t i = 0; i < spec.image.size(); ++i) {
    file[header_size + i] = spec.image[i];
  }

  return file;
}

// --- The VFS side --------------------------------------------------------

[[nodiscard]] std::span<const char> raw(std::string_view text) {
  return {text.data(), text.size()};
}

[[nodiscard]] dos_path exe_path(std::string_view leaf = "GAME.EXE") {
  dos_path result;
  const auto parsed = dos_name::parse(raw(leaf));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(result.push(parsed.value));
  return result;
}

[[nodiscard]] bool write_file(filesystem& fs, const dos_path& path,
                              const std::vector<std::uint8_t>& bytes) {
  const auto handle = fs.create(path);
  if (!handle.ok()) {
    return false;
  }
  const auto wrote = fs.write(handle.value, bytes);
  const bool ok = wrote.ok() && wrote.value == bytes.size();
  return fs.close(handle.value) == vfs_error::none && ok;
}

/// A machine and a filesystem, kept together because every test wants
/// both. On the heap: a machine has a megabyte inside it and
/// `memory_filesystem` carries 8 MiB of its own.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        fs(std::make_unique<memory_filesystem>()) {}

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] memory_filesystem& vfs() const noexcept { return *fs; }

  [[nodiscard]] std::uint8_t ram_byte(std::uint16_t segment,
                                      std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }

  [[nodiscard]] std::uint16_t ram_word(std::uint16_t segment,
                                       std::uint16_t offset) const {
    const auto lo = static_cast<unsigned>(ram_byte(segment, offset));
    const auto hi = static_cast<unsigned>(
        ram_byte(segment, static_cast<std::uint16_t>(offset + 1)));
    return static_cast<std::uint16_t>(lo | (hi << 8u));
  }

  /// Run until halted or stopped, the way a caller with no test harness
  /// of its own would.
  void run(unsigned cap = 2000) const {
    unsigned steps = 0;
    while (!box->processor().halted() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<memory_filesystem> fs;
};

/// The smallest legal image: one instruction, HLT, so a program that
/// does not care what it runs still leaves the machine somewhere sane.
[[nodiscard]] std::vector<std::uint8_t> halt_only() { return {0xF4}; }

// --- mz_header::decode ---------------------------------------------------

TEST(mz_header_decode, reads_every_field_of_a_well_formed_header) {
  const exe_spec spec{.initial_cs = 0x0001,
                      .initial_ip = 0x0010,
                      .initial_ss = 0x0002,
                      .initial_sp = 0x0100,
                      .min_alloc = 0x0005,
                      .relocations = {{.offset = 0x0004, .segment = 0x0000}},
                      .image = {0xF4, 0xF4, 0xF4, 0xF4, 0x00, 0x00}};
  const std::vector<std::uint8_t> file = build_exe(spec);
  const auto decoded =
      mz_header::decode(std::span(file).first(mz::encoded_size));

  ASSERT_TRUE(decoded.ok());
  EXPECT_EQ(decoded.value.relocation_count, 1u);
  EXPECT_EQ(decoded.value.min_alloc, 0x0005);
  EXPECT_EQ(decoded.value.initial_cs, 0x0001);
  EXPECT_EQ(decoded.value.initial_ip, 0x0010);
  EXPECT_EQ(decoded.value.initial_ss, 0x0002);
  EXPECT_EQ(decoded.value.initial_sp, 0x0100);
  EXPECT_EQ(decoded.value.relocation_table_offset, reloc_table_file_offset);
  EXPECT_EQ(decoded.value.header_size(),
            static_cast<std::uint32_t>(decoded.value.header_paragraphs) * 16u);
}

TEST(mz_header_decode, rejects_a_file_shorter_than_the_fixed_header) {
  const std::array<std::uint8_t, 10> short_file{'M', 'Z'};
  EXPECT_EQ(mz_header::decode(short_file).error, loader_error::bad_signature);
}

TEST(mz_header_decode, rejects_a_signature_that_is_not_mz) {
  std::array<std::uint8_t, mz::encoded_size> raw_header{};
  raw_header[0] = 'P';
  raw_header[1] = 'K';
  EXPECT_EQ(mz_header::decode(raw_header).error, loader_error::bad_signature);
}

// --- mz_header::image_length — the last-page-size rule -------------------

TEST(mz_header_image_length, treats_a_zero_last_page_as_a_full_one) {
  mz_header header{};
  header.last_page_size = 0;
  header.page_count = 3;
  const auto length = header.image_length();
  ASSERT_TRUE(length.ok());
  EXPECT_EQ(length.value, 3u * 512u);
}

TEST(mz_header_image_length, subtracts_the_unused_bytes_of_a_partial_page) {
  mz_header header{};
  header.last_page_size = 100;
  header.page_count = 3;
  const auto length = header.image_length();
  ASSERT_TRUE(length.ok());
  EXPECT_EQ(length.value, 2u * 512u + 100u);
}

TEST(mz_header_image_length, rejects_a_zero_page_count) {
  mz_header header{};
  header.page_count = 0;
  EXPECT_EQ(header.image_length().error, loader_error::bad_image_length);
}

TEST(mz_header_image_length, rejects_a_last_page_size_that_exceeds_one_page) {
  mz_header header{};
  header.page_count = 1;
  header.last_page_size = 513;
  EXPECT_EQ(header.image_length().error, loader_error::bad_image_length);
}

// --- load_program: validation, every field bounds-checked against the
//     file's own length (loader.h's top comment) --------------------------

TEST(loader_load_program, reports_file_error_for_a_missing_path) {
  const rig r;
  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::file_error);
}

TEST(loader_load_program,
     reports_bad_image_length_when_pages_overrun_the_file) {
  const rig r;
  std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                              .initial_ip = 0,
                                              .relocations = {},
                                              .image = halt_only()});
  // Claim ten times as many pages as the file actually has.
  put16(file, 0x04, 50);
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::bad_image_length);
}

TEST(loader_load_program, reports_bad_header_size_when_it_exceeds_the_image) {
  const rig r;
  std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                              .initial_ip = 0,
                                              .relocations = {},
                                              .image = halt_only()});
  // header_paragraphs claims more paragraphs than the whole file has.
  put16(file, 0x08, 0x1000);
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::bad_header_size);
}

TEST(loader_load_program,
     reports_bad_relocation_table_when_it_runs_past_the_header) {
  const rig r;
  std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                              .initial_ip = 0,
                                              .relocations = {},
                                              .image = halt_only()});
  // Claim far more relocation entries than the header has room for.
  put16(file, 0x06, 1000);
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::bad_relocation_table);
}

TEST(loader_load_program,
     reports_insufficient_memory_when_minalloc_cannot_fit) {
  const rig r;
  const std::vector<std::uint8_t> file =
      build_exe({.initial_cs = 0,
                 .initial_ip = 0,
                 .min_alloc = 0xFFFF,  // a full megabyte of paragraphs
                 .relocations = {},
                 .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::insufficient_memory);
}

TEST(loader_load_program,
     reports_relocation_out_of_image_for_a_target_outside_granted_memory) {
  const rig r;
  // 0xF000 paragraphs above the load segment lands in the BIOS region,
  // nowhere near the memory this program was granted.
  const std::vector<std::uint8_t> file =
      build_exe({.initial_cs = 0,
                 .initial_ip = 0,
                 .relocations = {{.offset = 0, .segment = 0xF000}},
                 .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::relocation_out_of_image);
}

TEST(loader_load_program, reports_bad_entry_point_outside_conventional_memory) {
  const rig r;
  const std::vector<std::uint8_t> file = build_exe({.initial_cs = 0xA000,
                                                    .initial_ip = 0,
                                                    .relocations = {},
                                                    .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  EXPECT_EQ(result.error, loader_error::bad_entry_point);
}

TEST(loader_load_program, reports_command_tail_too_long) {
  const rig r;
  const std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                                    .initial_ip = 0,
                                                    .relocations = {},
                                                    .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const std::string_view too_long(
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789"
      "01234567");  // 128 characters, over the 126-byte cap.
  const auto result = load_program(r.pc(), r.vfs(), exe_path(), raw(too_long));
  EXPECT_EQ(result.error, loader_error::command_tail_too_long);
}

// --- Relocations: the loader's whole reason to exist ----------------------

TEST(loader_relocations, patches_a_single_entry_by_the_load_segment) {
  const rig r;
  // A word at image offset 0, holding 0000h before relocation.
  const std::vector<std::uint8_t> file =
      build_exe({.initial_cs = 0,
                 .initial_ip = 2,
                 .relocations = {{.offset = 0, .segment = 0}},
                 .image = {0x00, 0x00, 0xF4}});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(r.ram_word(result.value.load_segment, 0),
            result.value.load_segment);
}

TEST(loader_relocations, patches_every_entry_of_several_independently) {
  const rig r;
  const std::vector<std::uint8_t> file = build_exe(
      {.initial_cs = 0,
       .initial_ip = 8,
       .relocations = {{.offset = 0, .segment = 0},
                       {.offset = 2, .segment = 0},
                       {.offset = 4, .segment = 0}},
       .image = {0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0xF4}});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());
  const std::uint16_t seg = result.value.load_segment;
  EXPECT_EQ(r.ram_word(seg, 0), static_cast<std::uint16_t>(0 + seg));
  EXPECT_EQ(r.ram_word(seg, 2), static_cast<std::uint16_t>(1 + seg));
  EXPECT_EQ(r.ram_word(seg, 4), static_cast<std::uint16_t>(2 + seg));
}

TEST(loader_relocations,
     wraps_the_high_byte_within_the_same_segment_at_offset_0xffff) {
  // loader.h's top comment: the one case that catches a shortcut. The
  // relocation's target is (segment 0, offset 0xFFFF) — its own codeSeg,
  // at the very top of it. The high byte of that word is offset 0x0000
  // of the *same* segment, not the paragraph physically after it: a
  // version that patched `physical_address(seg, 0xFFFF)` and
  // `physical_address(seg, 0xFFFF) + 1` instead would touch a
  // completely different segment and either leave this one's bytes
  // untouched or fail the granted-memory bounds check outright.
  // The entry point sits at image offset 1, not offset 0: offset 0 is
  // this fixture's word-to-relocate, and it has to start at the file's
  // own 0000h (unrelocated) rather than double as the HLT opcode.
  const rig r;
  const std::vector<std::uint8_t> file =
      build_exe({.initial_cs = 0,
                 .initial_ip = 1,
                 .relocations = {{.offset = 0xFFFF, .segment = 0}},
                 .image = {0x00, 0xF4}});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());
  const std::uint16_t seg = result.value.load_segment;

  // Before relocation both bytes are 0 (the file never wrote image
  // offset 0xFFFF, and RAM starts zeroed): the word wraps to exactly
  // `seg` once patched.
  EXPECT_EQ(r.ram_byte(seg, 0xFFFF), static_cast<std::uint8_t>(seg));
  EXPECT_EQ(r.ram_byte(seg, 0x0000), static_cast<std::uint8_t>(seg >> 8u));

  // And the byte physically after offset 0xFFFF — where a naive
  // `addr + 1` implementation would have written the high half instead
  // — was never touched.
  EXPECT_EQ(r.pc().memory().ram()[cpu::physical_address(seg, 0xFFFF) + 1], 0);
}

// --- The PSP -------------------------------------------------------------

TEST(loader_psp, writes_int20_top_of_memory_and_the_command_tail) {
  const rig r;
  const std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                                    .initial_ip = 0,
                                                    .relocations = {},
                                                    .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const std::string_view tail = "/C RUN";
  const auto result = load_program(r.pc(), r.vfs(), exe_path(), raw(tail));
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(r.ram_byte(psp_load_segment, psp::int20_offset), 0xCD);
  EXPECT_EQ(r.ram_byte(psp_load_segment, psp::int20_offset + 1), 0x20);

  EXPECT_EQ(r.ram_word(psp_load_segment, psp::top_of_memory_offset),
            static_cast<std::uint16_t>(conventional_ram_size / paragraph_size));

  // Parent and environment: documented placeholders, zero.
  EXPECT_EQ(r.ram_word(psp_load_segment, psp::parent_offset), 0);
  EXPECT_EQ(r.ram_word(psp_load_segment, psp::environment_offset), 0);

  // The command tail: count, bytes, CR.
  EXPECT_EQ(r.ram_byte(psp_load_segment, psp::command_tail_count_offset),
            tail.size());
  for (std::size_t i = 0; i < tail.size(); ++i) {
    EXPECT_EQ(
        r.ram_byte(psp_load_segment, static_cast<std::uint16_t>(
                                         psp::command_tail_bytes_offset + i)),
        static_cast<std::uint8_t>(tail[i]));
  }
  EXPECT_EQ(r.ram_byte(psp_load_segment,
                       static_cast<std::uint16_t>(
                           psp::command_tail_bytes_offset + tail.size())),
            0x0D);
}

// --- Entry state -----------------------------------------------------------

TEST(loader_entry_state, sets_segments_stack_and_ax_with_clean_flags) {
  const rig r;
  const std::vector<std::uint8_t> file =
      build_exe({.initial_cs = 0x0010,
                 .initial_ip = 0x0004,
                 .initial_ss = 0x0010,
                 .initial_sp = 0x0200,
                 .relocations = {},
                 .image = {0xF4, 0xF4, 0xF4, 0xF4, 0xF4}});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());

  const cpu::registers& regs = r.pc().processor().regs();
  EXPECT_EQ(regs[cpu::sreg::ds], psp_load_segment);
  EXPECT_EQ(regs[cpu::sreg::es], psp_load_segment);
  EXPECT_EQ(regs[cpu::sreg::cs], result.value.entry_cs);
  EXPECT_EQ(regs.ip, result.value.entry_ip);
  EXPECT_EQ(regs[cpu::sreg::ss], result.value.entry_ss);
  EXPECT_EQ(regs[cpu::reg16::sp], result.value.entry_sp);
  EXPECT_EQ(regs[cpu::reg16::ax], 0x0000);
  EXPECT_EQ(regs.flags, cpu::flag::reset_value);

  EXPECT_EQ(result.value.entry_cs,
            static_cast<std::uint16_t>(0x0010 + image_load_segment));
  EXPECT_EQ(result.value.entry_ip, 0x0004);
  EXPECT_EQ(result.value.entry_ss,
            static_cast<std::uint16_t>(0x0010 + image_load_segment));
  EXPECT_EQ(result.value.entry_sp, 0x0200);
  EXPECT_EQ(result.value.psp_segment, psp_load_segment);
  EXPECT_EQ(result.value.load_segment, image_load_segment);
}

// --- Exit propagation: the PSP's INT 20h ------------------------------------

TEST(loader_exit, int20_stops_the_machine_with_exit_code_zero) {
  const rig r;
  //   0000  CD 20   INT 20h
  const std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                                    .initial_ip = 0,
                                                    .relocations = {},
                                                    .image = {0xCD, 0x20}});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());

  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::program_exited);
  EXPECT_EQ(r.pc().stop().exit_code, 0);

  ASSERT_FALSE(r.log.stops.empty());
  EXPECT_EQ(r.log.stops.back(), r.pc().stop());
}

TEST(loader_exit, falling_into_the_psp_from_the_end_of_the_image_terminates) {
  // The PSP's INT 20h at offset 0 is a real instruction, not a marker
  // (loader.h's top comment): a program that never executes an explicit
  // INT 20h but falls off the end of its own code into the PSP still
  // terminates instead of running whatever garbage follows.
  const rig r;
  //   0000  EB FE   JMP $ would loop forever, so instead: nothing at
  //   all — CS:IP starts right at the PSP's own INT 20h by pointing the
  //   entry point at the PSP segment directly. That is not how a linker
  //   would ever build an EXE, so this is exercised directly rather than
  //   through an EXE fixture.
  const std::vector<std::uint8_t> file = build_exe({.initial_cs = 0,
                                                    .initial_ip = 0,
                                                    .relocations = {},
                                                    .image = halt_only()});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));
  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());

  // Redirect execution at the PSP's own INT 20h, exactly where a program
  // that fell off the end of its code would land.
  cpu::registers& regs = r.pc().processor().regs();
  regs[cpu::sreg::cs] = psp_load_segment;
  regs.ip = psp::int20_offset;

  r.run();

  EXPECT_EQ(r.pc().stop().reason, stop_reason::program_exited);
  EXPECT_EQ(r.pc().stop().exit_code, 0);
}

// --- The exit criterion: a multi-segment EXE with relocations runs to
//     its computed answer through the machine ------------------------------

TEST(loader_exit_criterion,
     a_self_written_multi_segment_exe_with_relocations_runs_to_its_answer) {
  // Two logical segments inside one image: codeSeg at paragraph 0 (file
  // offset 0) and dataSeg at paragraph 1 (file offset 0x10), each
  // referenced by a relocatable segment immediate the way a linker
  // would emit one for a program with more than one SEGMENT directive.
  //
  // codeSeg (entry point):
  //   0000  B8 imm16        MOV AX, dataSeg          ; relocated: seg 0, off 1
  //   0003  8E D8           MOV DS, AX
  //   0005  A1 00 00        MOV AX, [0000]           ; dataSeg:0000 = 0x1234
  //   0008  05 11 01        ADD AX, 0111h
  //   000B  A3 02 00        MOV [0002], AX            ; dataSeg:0002 = answer
  //   000E  F4              HLT
  //
  // dataSeg (paragraph 1, file offset 0x10):
  //   0000  34 12           dw 1234h
  //   0002  00 00           (the answer goes here)
  constexpr std::uint16_t data_paragraph = 1;
  constexpr std::uint16_t seed = 0x1234;
  constexpr std::uint16_t addend = 0x0111;

  std::vector<std::uint8_t> image(0x10 + 4, 0);
  // codeSeg, at image offset 0.
  image[0x00] = 0xB8;
  image[0x01] = static_cast<std::uint8_t>(data_paragraph);        // low(imm)
  image[0x02] = static_cast<std::uint8_t>(data_paragraph >> 8u);  // high(imm)
  image[0x03] = 0x8E;
  image[0x04] = 0xD8;
  image[0x05] = 0xA1;
  image[0x06] = 0x00;
  image[0x07] = 0x00;
  image[0x08] = 0x05;
  image[0x09] = static_cast<std::uint8_t>(addend);
  image[0x0A] = static_cast<std::uint8_t>(addend >> 8u);
  image[0x0B] = 0xA3;
  image[0x0C] = 0x02;
  image[0x0D] = 0x00;
  image[0x0E] = 0xF4;
  // dataSeg, at image offset 0x10 (paragraph 1).
  image[0x10] = static_cast<std::uint8_t>(seed);
  image[0x11] = static_cast<std::uint8_t>(seed >> 8u);

  const rig r;
  const std::vector<std::uint8_t> file = build_exe(
      {.initial_cs = 0,
       .initial_ip = 0,
       .initial_ss = 0,
       .initial_sp = 0x0100,
       .relocations = {{.offset = 0x01, .segment = 0}},  // the MOV AX,dataSeg
                                                         // immediate
       .image = image});
  ASSERT_TRUE(write_file(r.vfs(), exe_path(), file));

  const auto result = load_program(r.pc(), r.vfs(), exe_path());
  ASSERT_TRUE(result.ok());

  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.pc().processor().regs()[cpu::reg16::ax],
            static_cast<std::uint16_t>(seed + addend));

  // And the machine computed it with real, relocated multi-segment
  // addressing: the answer is sitting in dataSeg, at its real load
  // segment, not at the file's own segment-0 numbering.
  const auto data_segment =
      static_cast<std::uint16_t>(result.value.load_segment + data_paragraph);
  EXPECT_EQ(r.ram_word(data_segment, 2),
            static_cast<std::uint16_t>(seed + addend));
}

}  // namespace
}  // namespace amberfolio::machine
