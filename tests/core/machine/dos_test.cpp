// SPDX-License-Identifier: AGPL-3.0-only
//
// The INT 21h subset (M2-D7, #52): the issue's own exit criterion — a
// program creates, writes, reopens, seeks, reads back and unlinks a file,
// verifying contents and every error path — plus the standard handles,
// date/time, console output, exit and the Ctrl-Break detour.
//
// Every test drives a real `INT 21h` (or `INT 20h`) through the machine,
// the way service_floor_test.cpp drives its own vector: a program cannot
// tell the difference between a native handler and a hooked one, so a
// test that called a handler function directly would not be testing what
// a program actually observes.
//
// Every byte of every program below is written here, from the encoding —
// the clean-content rule applies to test data exactly as it does to
// everything else (PLAN.md §6).

#include "amberfolio/machine/dos.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/vfs.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t stack_top = 0x1000;

/// Where a test pokes an ASCIZ pathname before pointing DS:DX at it.
constexpr std::uint16_t path_area = 0x0080;

/// Where a test pokes or reads back file/console data. Comfortably past
/// `path_area` and comfortably below the stack, with room for more than
/// one `io_chunk_size` (512, dos.cpp) so the chunking loop itself is
/// exercised, not just the single-chunk case.
constexpr std::uint16_t data_area = 0x0200;

/// A machine wired up exactly as `install_dos_services()` expects: a
/// filesystem attached before the vectors are installed, and a sink that
/// keeps what it is told instead of printing it.
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

  void poke(std::uint16_t at, std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t p = cpu::physical_address(code_segment, at);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[p] = byte;
      ++p;
    }
  }

  /// Write `text` at `code_segment:at`, NUL-terminated — an ASCIZ
  /// pathname (or, without the terminator mattering, an AH=09h string)
  /// the way a program would have it in its own data segment.
  void write_asciz(std::uint16_t at, std::string_view text) const {
    std::uint32_t p = cpu::physical_address(code_segment, at);
    for (const char c : text) {
      box->memory().ram()[p] = static_cast<std::uint8_t>(c);
      ++p;
    }
    box->memory().ram()[p] = 0;
  }

  [[nodiscard]] std::uint8_t byte_at(std::uint16_t at) const {
    return box->memory().ram()[cpu::physical_address(code_segment, at)];
  }

  /// Load `INT 21h ; HLT` at offset 0 and point the processor at it, with
  /// a stack — every call here takes an interrupt.
  void program() const {
    poke(0, {0xCD, 0x21, 0xF4});
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = code_segment;
    r[cpu::sreg::ss] = code_segment;
    r[cpu::reg16::sp] = stack_top;
    r.ip = 0;
  }

  /// Same, but `INT 20h ; HLT` — the PSP-style terminator.
  void program20() const {
    poke(0, {0xCD, 0x20, 0xF4});
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = code_segment;
    r[cpu::sreg::ss] = code_segment;
    r[cpu::reg16::sp] = stack_top;
    r.ip = 0;
  }

  void run(unsigned cap = 4000) const {
    unsigned steps = 0;
    while (!box->processor().halted() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  /// Set up AX/BX/CX/DX/DS, run `INT 21h` to the HLT (or a stop) and
  /// answer whether the machine halted rather than stopped — the shape
  /// every test below wants, since most of them care about AX and CF
  /// afterward and nothing else.
  void call(std::uint16_t ax, std::uint16_t bx = 0, std::uint16_t cx = 0,
            std::uint16_t dx = 0, std::uint16_t ds = code_segment) const {
    program();
    cpu::registers& r = regs();
    r[cpu::reg16::ax] = ax;
    r[cpu::reg16::bx] = bx;
    r[cpu::reg16::cx] = cx;
    r[cpu::reg16::dx] = dx;
    r[cpu::sreg::ds] = ds;
    run();
  }

  [[nodiscard]] bool carry() const { return regs().flag_set(cpu::flag::cf); }

  recording_diagnostics log;
  std::unique_ptr<memory_filesystem> fs;
  std::unique_ptr<machine> box;
};

[[nodiscard]] std::uint16_t ah(std::uint8_t function) {
  return static_cast<std::uint16_t>(function) << 8u;
}

// --- The file-activity channel (M4-G3/#104, M4-G4/#105) ----------------
//
// A service call says AH=3Dh and where it came from; only the DOS layer
// knows which file, and it does not know that until the handler has
// resolved the path (diagnostics.h). These are the tests that the answer
// reaches the sink - the path canonical, the handle the one the program
// got back, and the refusals reported rather than swallowed, because "is
// there a save in slot A" is a question a program asks by opening a file
// and no is an answer.

/// The events the last call produced, in order.
[[nodiscard]] std::vector<file_event> events(const rig& r) {
  return r.log.files;
}

TEST(dos_file_events, a_create_names_the_path_and_the_handle_it_answered) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].what, file_action::create);
  EXPECT_EQ(got[0].handle, r.regs()[cpu::reg16::ax]);
  EXPECT_EQ(got[0].error, vfs_error::none);
  EXPECT_TRUE(got[0].ok());

  std::array<char, dos_path_capacity> text{};
  format_dos_path(got[0].path, text);
  EXPECT_STREQ(text.data(), "\\GAME.DAT");
}

TEST(dos_file_events, an_open_that_fails_is_reported_with_its_reason) {
  rig r;
  r.write_asciz(path_area, "MISSING.DAT");
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_TRUE(r.carry());

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].what, file_action::open);
  EXPECT_EQ(got[0].error, vfs_error::file_not_found);
  EXPECT_FALSE(got[0].ok());
  // No handle to name, and the field says so rather than carrying the
  // last one that happened to be there.
  EXPECT_EQ(got[0].handle, 0U);
}

TEST(dos_file_events, a_close_carries_the_path_the_handle_was_opened_on) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];

  r.log.files.clear();
  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].what, file_action::close);
  EXPECT_EQ(got[0].handle, handle);
  std::array<char, dos_path_capacity> text{};
  format_dos_path(got[0].path, text);
  EXPECT_STREQ(text.data(), "\\GAME.DAT");
}

TEST(dos_file_events, a_close_says_whether_anything_moved_through_it) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  std::uint16_t handle = r.regs()[cpu::reg16::ax];

  // Written through: four bytes out of the caller's buffer.
  r.write_asciz(static_cast<std::uint16_t>(path_area + 0x40), "abcd");
  r.call(ah(0x40), handle, 4, static_cast<std::uint16_t>(path_area + 0x40));
  ASSERT_FALSE(r.carry());
  r.log.files.clear();
  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());
  {
    const std::vector<file_event> got = events(r);
    ASSERT_EQ(got.size(), 1U);
    EXPECT_TRUE(got[0].written_through);
    EXPECT_FALSE(got[0].read_through);
  }

  // Opened and given straight back — which is what the program does to
  // every save slot in turn when it lists them, and is not a read.
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  handle = r.regs()[cpu::reg16::ax];
  r.log.files.clear();
  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());
  {
    const std::vector<file_event> got = events(r);
    ASSERT_EQ(got.size(), 1U);
    EXPECT_FALSE(got[0].read_through) << "nothing was read out of it";
    EXPECT_FALSE(got[0].written_through);
  }

  // And now one that is read.
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  handle = r.regs()[cpu::reg16::ax];
  r.call(ah(0x3F), handle, 4, static_cast<std::uint16_t>(path_area + 0x40));
  ASSERT_FALSE(r.carry());
  ASSERT_EQ(r.regs()[cpu::reg16::ax], 4U);
  r.log.files.clear();
  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());
  {
    const std::vector<file_event> got = events(r);
    ASSERT_EQ(got.size(), 1U);
    EXPECT_TRUE(got[0].read_through);
  }
}

TEST(dos_file_events, mkdir_and_unlink_are_named_too) {
  rig r;
  r.write_asciz(path_area, "SAVE");
  r.call(ah(0x39), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  r.write_asciz(path_area, "SAVE\\SLOT.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x3E), r.regs()[cpu::reg16::ax]);
  ASSERT_FALSE(r.carry());
  r.write_asciz(path_area, "SAVE\\SLOT.DAT");
  r.call(ah(0x41), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 4U);
  EXPECT_EQ(got[0].what, file_action::mkdir);
  EXPECT_EQ(got[1].what, file_action::create);
  EXPECT_EQ(got[2].what, file_action::close);
  EXPECT_EQ(got[3].what, file_action::unlink);

  std::array<char, dos_path_capacity> text{};
  format_dos_path(got[3].path, text);
  EXPECT_STREQ(text.data(), "\\SAVE\\SLOT.DAT");
}

TEST(dos_file_events, a_second_mkdir_of_the_same_name_reports_the_refusal) {
  // What a Gold Box save does on every visit to the camp screen: make the
  // save directory, ignore the answer, and write into it. The refusal is
  // ordinary, and the point of reporting it is that a reader of a log can
  // tell it from a refusal that mattered.
  rig r;
  r.write_asciz(path_area, "SAVE");
  r.call(ah(0x39), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  r.log.files.clear();

  r.write_asciz(path_area, "SAVE");
  r.call(ah(0x39), 0, 0, path_area);
  EXPECT_TRUE(r.carry());

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].what, file_action::mkdir);
  EXPECT_EQ(got[0].error, vfs_error::access_denied);
}

TEST(dos_file_events, the_reads_and_writes_in_between_say_nothing_here) {
  // The channel is the naming calls and nothing else: a run's file
  // activity is which files it touched, and a line per 512-byte chunk
  // would bury that under the thing the service trace already shows.
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.write_asciz(data_area, "DATA");
  r.log.files.clear();

  r.call(ah(0x40), handle, 4, data_area);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x42), handle, 0, 0);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x3F), handle, 4, data_area);
  ASSERT_FALSE(r.carry());

  EXPECT_TRUE(events(r).empty());
}

// --- The same thing in the trace report (#121) -------------------------
//
// The live stream above is what a host prints as a run happens. The trace
// report is what a reader reads afterwards, and until #121 it had the
// service calls and not the file events - so `INT21 ax=3D00 handled` was
// the whole of what a report said about a program that had spent its
// startup asking for a file that is not there. These are the tests that
// the tail carries the path and the answer, and that a run which opened
// what it meant to does not read like one that did not.

[[nodiscard]] std::string trace_text(const rig& r) {
  std::vector<char> buffer(trace_report_capacity);
  const std::size_t n = format_trace_report(r.pc(), buffer);
  return {buffer.data(), n};
}

[[nodiscard]] bool has(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

TEST(dos_file_trace, a_failed_open_names_the_path_and_what_dos_answered) {
  rig r;
  r.pc().trace().enable(true);
  r.write_asciz(path_area, "POR\\POOL.CFG");
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_TRUE(r.carry());

  const std::string text = trace_text(r);
  EXPECT_TRUE(has(text,
                  "trace file=open \\POR\\POOL.CFG handle=0000 "
                  "path_not_found from="))
      << text;
  EXPECT_TRUE(has(text, "files_seen=1 kept=1")) << text;
}

TEST(dos_file_trace, tells_not_there_from_no_handles_left) {
  // The three answers the report has to keep apart, named rather than
  // numbered: `vfs_error_name` spells each one as its own enumerator, so
  // a line in a log and a case in vfs.h are searchable with one string.
  rig r;
  r.pc().trace().enable(true);
  r.write_asciz(path_area, "SAVE");
  r.call(ah(0x39), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x39), 0, 0, path_area);
  ASSERT_TRUE(r.carry());
  r.write_asciz(path_area, "MISSING.DAT");
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_TRUE(r.carry());

  const std::string text = trace_text(r);
  EXPECT_TRUE(has(text, "trace file=mkdir \\SAVE handle=0000 none from="))
      << text;
  EXPECT_TRUE(
      has(text, "trace file=mkdir \\SAVE handle=0000 access_denied from="))
      << text;
  EXPECT_TRUE(has(text,
                  "trace file=open \\MISSING.DAT handle=0000 "
                  "file_not_found from="))
      << text;
}

TEST(dos_file_trace, an_open_that_worked_is_not_read_as_one_that_did_not) {
  rig r;
  r.pc().trace().enable(true);
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  const std::string text = trace_text(r);
  // The handle it actually answered with, and `none` where a failure
  // would carry its reason.
  EXPECT_TRUE(has(text, "trace file=create \\GAME.DAT handle=0005 none from="))
      << text;
  EXPECT_FALSE(has(text, "file_not_found")) << text;
  EXPECT_FALSE(has(text, "path_not_found")) << text;
}

TEST(dos_file_trace, a_name_that_does_not_resolve_at_all_is_reported_too) {
  // The one naming failure this machine used to report nowhere: the name
  // is refused by `canonicalize()` before the filesystem is consulted, so
  // no path exists to name and the event carries the root. A program
  // asking for a file on a drive this machine does not have is exactly
  // how "please insert the disk" happens with nothing in the log.
  rig r;
  r.pc().trace().enable(true);
  r.write_asciz(path_area, "A:\\POOL.CFG");
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], dos_error_code(vfs_error::invalid_drive));

  const std::vector<file_event> got = events(r);
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].what, file_action::open);
  EXPECT_EQ(got[0].error, vfs_error::invalid_drive);
  EXPECT_TRUE(got[0].path.is_root());

  EXPECT_TRUE(
      has(trace_text(r), "trace file=open \\ handle=0000 invalid_drive from="))
      << trace_text(r);
}

TEST(dos_file_trace, keeps_nothing_when_nobody_asked_for_a_trace) {
  // The rule the whole facility rests on: off unless asked for, and a run
  // with it off is the same run. The sink still hears the event - that
  // channel is the host's to filter - but the ring is empty and the
  // report says so.
  rig r;
  r.write_asciz(path_area, "MISSING.DAT");
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_TRUE(r.carry());

  EXPECT_EQ(events(r).size(), 1U) << "the sink is a separate channel";
  EXPECT_EQ(r.pc().trace().files_seen(), 0U);
  EXPECT_EQ(trace_text(r), "amberfolio: stop trace=off\n");
}

// --- The exit criterion: create, write, reopen, seek, read, unlink -----

TEST(dos_file_io, creates_writes_reopens_seeks_reads_back_and_unlinks) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  constexpr std::string_view payload = "AMBER FOLIO TEST DATA";
  r.write_asciz(data_area, payload);

  // AH=3Ch create: the standard handles fill 0-4, so this is handle 5,
  // exactly as programs written against DOS expect.
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  EXPECT_EQ(handle, 5u);

  // AH=40h write.
  r.call(ah(0x40), handle, static_cast<std::uint16_t>(payload.size()),
         data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], payload.size());

  // AH=3Eh close.
  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());

  // AH=3Dh reopen, read-only: the slot close() freed is handle 5 again.
  r.call(ah(0x3D), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], handle);

  // AH=42h seek to the start (a program cannot assume a fresh open()
  // already sits at 0 in this subset's own tests, even though it does).
  r.call(ah(0x42) | 0, handle, 0, 0);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0u);
  EXPECT_EQ(r.regs()[cpu::reg16::dx], 0u);

  // AH=3Fh read the whole thing back.
  r.call(ah(0x3F), handle, static_cast<std::uint16_t>(payload.size()),
         data_area + 0x0100);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], payload.size());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(r.byte_at(static_cast<std::uint16_t>(data_area + 0x0100 + i)),
              static_cast<std::uint8_t>(payload[i]))
        << "byte " << i;
  }

  r.call(ah(0x3E), handle);
  ASSERT_FALSE(r.carry());

  // AH=41h unlink.
  r.call(ah(0x41), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  // And it is really gone: reopening it fails.
  r.call(ah(0x3D), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::file_not_found));
}

TEST(dos_file_io, moves_more_than_one_io_chunk_on_read_and_write) {
  rig r;
  r.write_asciz(path_area, "BIG.DAT");

  // io_chunk_size (dos.cpp) is 512; 700 bytes forces the loop around
  // twice on both sides.
  constexpr std::uint16_t length = 700;
  for (std::uint16_t i = 0; i < length; ++i) {
    r.poke(static_cast<std::uint16_t>(data_area + i),
           {static_cast<std::uint8_t>(i)});
  }

  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];

  r.call(ah(0x40), handle, length, data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], length);

  r.call(ah(0x42) | 0, handle, 0, 0);
  ASSERT_FALSE(r.carry());

  r.call(ah(0x3F), handle, length, data_area + 0x0400);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], length);
  for (std::uint16_t i = 0; i < length; ++i) {
    EXPECT_EQ(r.byte_at(static_cast<std::uint16_t>(data_area + 0x0400 + i)),
              static_cast<std::uint8_t>(i))
        << "byte " << i;
  }
}

TEST(dos_file_io, size_zero_write_truncates_at_the_current_position) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.write_asciz(data_area, "0123456789");

  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(ah(0x40), handle, 10, data_area);
  ASSERT_FALSE(r.carry());

  // Seek to the middle, then write zero bytes: the file shrinks to five.
  r.call(ah(0x42) | 0, handle, 0, 5);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x40), handle, 0, data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0u);

  r.call(ah(0x42) | 2, handle, 0, 0);  // seek from end, offset 0
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 5u);
}

// --- Open/create error paths --------------------------------------------

TEST(dos_open, fails_on_a_file_that_does_not_exist) {
  rig r;
  r.write_asciz(path_area, "NOPE.DAT");
  r.call(ah(0x3D), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::file_not_found));
}

TEST(dos_open, fails_when_the_directory_does_not_exist) {
  rig r;
  r.write_asciz(path_area, "NODIR\\GAME.DAT");
  r.call(ah(0x3D), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::path_not_found));
}

TEST(dos_open, validates_the_access_mode) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  r.call(ah(0x3E), r.regs()[cpu::reg16::ax]);

  // AL=3 is outside 0-2 (read/write/read-write).
  r.call(ah(0x3D) | 3, 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], invalid_access_code);
}

TEST(dos_read, refuses_a_write_only_handle) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(ah(0x3E), handle);

  r.call(ah(0x3D) | 1, 0, 0, path_area);  // write-only
  ASSERT_FALSE(r.carry());
  const std::uint16_t wo = r.regs()[cpu::reg16::ax];

  r.call(ah(0x3F), wo, 1, data_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], dos_error_code(vfs_error::access_denied));
}

TEST(dos_write, refuses_a_read_only_handle) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];
  r.call(ah(0x3E), handle);

  r.call(ah(0x3D) | 0, 0, 0, path_area);  // read-only
  ASSERT_FALSE(r.carry());
  const std::uint16_t ro = r.regs()[cpu::reg16::ax];

  r.call(ah(0x40), ro, 1, data_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], dos_error_code(vfs_error::access_denied));
}

// --- Close/unlink/mkdir error paths -------------------------------------

TEST(dos_close, fails_on_a_handle_that_is_not_open) {
  rig r;
  r.call(ah(0x3E), 5);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::invalid_handle));
}

TEST(dos_unlink, fails_on_a_file_that_does_not_exist) {
  rig r;
  r.write_asciz(path_area, "NOPE.DAT");
  r.call(ah(0x41), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::file_not_found));
}

TEST(dos_mkdir, fails_when_the_name_is_already_taken) {
  rig r;
  r.write_asciz(path_area, "SAVE");
  r.call(ah(0x39), 0, 0, path_area);
  ASSERT_FALSE(r.carry());

  r.call(ah(0x39), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], dos_error_code(vfs_error::access_denied));
}

// --- Seek -----------------------------------------------------------------

TEST(dos_seek, returns_the_32_bit_position_in_dx_ax) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];

  // Past 64K, so DX has to carry the high half or the test is trivial.
  constexpr std::uint32_t target = 0x00010203;
  r.program();
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::ax] = ah(0x42);  // AL = 0, seek from start
  regs[cpu::reg16::bx] = handle;
  regs[cpu::reg16::cx] = static_cast<std::uint16_t>(target >> 16u);
  regs[cpu::reg16::dx] = static_cast<std::uint16_t>(target);
  r.run();

  ASSERT_FALSE(r.carry());
  const auto got =
      (static_cast<std::uint32_t>(r.regs()[cpu::reg16::dx]) << 16u) |
      r.regs()[cpu::reg16::ax];
  EXPECT_EQ(got, target);
}

TEST(dos_seek, rejects_an_origin_outside_0_2) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];

  r.call(static_cast<std::uint16_t>(ah(0x42) | 3), handle);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], invalid_function_code);
}

TEST(dos_seek, refuses_a_non_file_handle) {
  rig r;
  r.call(ah(0x42), 1);  // handle 1: STDOUT, a console handle
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::invalid_handle));
}

// --- The DOS handle table -------------------------------------------------

TEST(dos_handles, reports_too_many_open_files_when_the_table_is_full) {
  rig r;

  // Five standard handles are already open; twenty is the table's whole
  // capacity (dos.h), so fifteen more creates fill it exactly.
  for (unsigned i = 0; i < 15; ++i) {
    char name[13];
    std::snprintf(name, sizeof name, "F%u.DAT", i);
    r.write_asciz(path_area, name);
    r.call(ah(0x3C), 0, 0, path_area);
    ASSERT_FALSE(r.carry()) << "file " << i;
    ASSERT_EQ(r.regs()[cpu::reg16::ax], 5u + i) << "file " << i;
  }

  r.write_asciz(path_area, "ONEMORE.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::too_many_open_files));
}

// --- The standard handles ---------------------------------------------

TEST(dos_standard_handles,
     the_documented_sink_reads_nothing_and_discards_writes) {
  rig r;

  // Handle 0: STDIN, one of the documented-sink handles — no keyboard
  // service exists yet (#53), so the honest answer is "no data".
  r.call(ah(0x3F), 0, 10, data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0u);

  // Handle 3 (STDAUX): a write reports every byte accepted, and nothing
  // reaches the console — dos.h's top comment.
  r.write_asciz(data_area, "IGNORED");
  r.call(ah(0x40), 3, 7, data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 7u);

  std::array<std::uint8_t, 16> drained{};
  EXPECT_EQ(r.pc().console().read(drained), 0u);
}

TEST(dos_standard_handles, ah40_on_handle_two_reaches_the_console_sink) {
  rig r;
  r.write_asciz(data_area, "STDERR LINE");
  r.call(ah(0x40), 2, 11, data_area);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 11u);

  std::array<std::uint8_t, 16> drained{};
  const std::size_t got = r.pc().console().read(drained);
  ASSERT_EQ(got, 11u);
  EXPECT_EQ(drained[0], 'S');
}

// --- Console output: AH=02h, AH=09h --------------------------------------

TEST(dos_console, ah02_writes_one_character) {
  rig r;
  r.call(ah(0x02), 0, 0, static_cast<std::uint8_t>('X'));
  ASSERT_FALSE(r.carry());

  std::array<std::uint8_t, 4> drained{};
  ASSERT_EQ(r.pc().console().read(drained), 1u);
  EXPECT_EQ(drained[0], 'X');
}

TEST(dos_console, ah09_writes_until_the_dollar_sign) {
  rig r;
  r.write_asciz(data_area, "HELLO$UNSEEN");
  r.call(ah(0x09), 0, 0, data_area);
  ASSERT_FALSE(r.carry());

  std::array<std::uint8_t, 16> drained{};
  const std::size_t got = r.pc().console().read(drained);
  ASSERT_EQ(got, 5u);
  EXPECT_EQ(drained[0], 'H');
  EXPECT_EQ(drained[4], 'O');
}

// --- Date/time: AH=2Ah, AH=2Ch ---------------------------------------------

TEST(dos_datetime, ah2a_reads_the_seeded_wall_clock) {
  rig r;
  ASSERT_TRUE(r.pc().set_wall_time(
      {.year = 1999, .month = 3, .day = 4, .hour = 0, .minute = 0}));

  r.call(ah(0x2A));
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::cx], 1999u);
  EXPECT_EQ(r.regs().get(cpu::reg8::dh), 3u);
  EXPECT_EQ(r.regs().get(cpu::reg8::dl), 4u);
}

TEST(dos_datetime, ah2c_reads_the_seeded_wall_clock) {
  rig r;
  ASSERT_TRUE(r.pc().set_wall_time({.year = 1999,
                                    .month = 3,
                                    .day = 4,
                                    .hour = 13,
                                    .minute = 45,
                                    .second = 6}));

  r.call(ah(0x2C));
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs().get(cpu::reg8::ch), 13u);
  EXPECT_EQ(r.regs().get(cpu::reg8::cl), 45u);
  EXPECT_EQ(r.regs().get(cpu::reg8::dh), 6u);
}

// --- Exit: AH=4Ch, INT 20h -------------------------------------------------

TEST(dos_exit, ah4c_stores_the_code_and_stops_the_machine) {
  rig r;
  r.call(ah(0x4C) | 0x07);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::program_exited);
  EXPECT_EQ(r.pc().stop().exit_code, 7u);
  EXPECT_TRUE(r.pc().dos().exited());
  EXPECT_EQ(r.pc().dos().exit_code(), 7u);
}

TEST(dos_exit, int20h_stops_with_code_zero) {
  rig r;
  r.program20();
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::program_exited);
  EXPECT_EQ(r.pc().stop().exit_code, 0u);
}

// --- Everything else logs and stops ----------------------------------

// --- AH=25h / AH=35h: the interrupt vectors -------------------------------

TEST(dos_vectors, ah25_writes_the_real_vector_table_entry) {
  rig r;
  r.call(ah(0x25) | 0x60, 0, 0, 0xBEEF, 0x1234);

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.carry());

  cpu::processor& cpu = r.pc().processor();
  const std::uint16_t at = cpu::vector_table_offset(0x60);
  EXPECT_EQ(cpu.read_word(cpu::vector_table_segment, at), 0xBEEFu);
  EXPECT_EQ(cpu.read_word(cpu::vector_table_segment,
                          static_cast<std::uint16_t>(at + 2)),
            0x1234u);
}

TEST(dos_vectors, ah35_reads_it_back_into_es_bx) {
  rig r;
  r.call(ah(0x25) | 0x60, 0, 0, 0xBEEF, 0x1234);
  ASSERT_FALSE(r.carry());

  r.call(ah(0x35) | 0x60);
  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::bx], 0xBEEFu);
  EXPECT_EQ(r.regs()[cpu::sreg::es], 0x1234u);
}

TEST(dos_vectors, ah35_sees_a_vector_the_program_stored_by_hand) {
  rig r;

  // Four bytes at 0000:0084 *is* hooking INT 21h's neighbour, and this
  // layer has no table of its own that could disagree (service_floor.h).
  cpu::processor& cpu = r.pc().processor();
  const std::uint16_t at = cpu::vector_table_offset(0x21);
  cpu.write_word(cpu::vector_table_segment, at, 0x0042);
  cpu.write_word(cpu::vector_table_segment, static_cast<std::uint16_t>(at + 2),
                 0x9000);

  // Reached through INT 20h, since INT 21h no longer points at us.
  r.program20();
  r.run();
  EXPECT_TRUE(r.pc().stopped());
}

TEST(dos_vectors, ah35_of_an_unhooked_vector_answers_this_machines_stub) {
  rig r;
  r.call(ah(0x35) | 0x10);

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::sreg::es], service::stub_segment);
  EXPECT_EQ(r.regs()[cpu::reg16::bx], service::stub_offset(0x10));
}

// --- AH=44h AL=00h: what is behind a handle -------------------------------

TEST(dos_ioctl, the_console_handles_are_character_devices) {
  rig r;
  r.call(ah(0x44) | 0x00, 1);

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.carry());
  // Bit 7 (character device) and bit 1 (the console output device), and
  // nothing else — bit 6 stays clear because a read of this handle
  // answers zero bytes, which is end of file (dos.cpp).
  EXPECT_EQ(r.regs()[cpu::reg16::dx], 0x0082u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x0082u);
}

TEST(dos_ioctl, the_documented_sink_handles_are_the_nul_device) {
  rig r;
  for (const std::uint16_t handle :
       {std::uint16_t{0}, std::uint16_t{3}, std::uint16_t{4}}) {
    r.call(ah(0x44) | 0x00, handle);
    ASSERT_FALSE(r.carry()) << "handle " << handle;
    EXPECT_EQ(r.regs()[cpu::reg16::dx], 0x0084u) << "handle " << handle;
  }
}

TEST(dos_ioctl, a_file_reports_the_drive_and_whether_it_has_been_written) {
  rig r;
  r.write_asciz(path_area, "GAME.DAT");
  r.call(ah(0x3C), 0, 0, path_area);
  ASSERT_FALSE(r.carry());
  const std::uint16_t handle = r.regs()[cpu::reg16::ax];

  // Freshly created: bit 7 clear (a file), the drive in bits 0-5, and
  // bit 6 set because nothing has gone through it yet.
  r.call(ah(0x44) | 0x00, handle);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::dx],
            static_cast<std::uint16_t>(only_drive | 0x40u));

  r.write_asciz(data_area, "X");
  r.call(ah(0x40), handle, 1, data_area);
  ASSERT_FALSE(r.carry());

  r.call(ah(0x44) | 0x00, handle);
  ASSERT_FALSE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::dx], std::uint16_t{only_drive});
}

TEST(dos_ioctl, a_handle_that_names_nothing_open_fails) {
  rig r;
  r.call(ah(0x44) | 0x00, 19);

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_TRUE(r.carry());
  EXPECT_EQ(r.regs()[cpu::reg16::ax],
            dos_error_code(vfs_error::invalid_handle));
}

TEST(dos_ioctl, setting_device_information_is_deferred) {
  rig r;
  r.call(ah(0x44) | 0x01, 1);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_service);
}

TEST(dos_dispatch, an_unknown_function_logs_and_stops) {
  rig r;
  r.call(ah(0x60));  // Never a DOS function this machine will implement.

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_service);
  ASSERT_FALSE(r.log.calls.empty());
  EXPECT_EQ(r.log.calls.back().function(), 0x60);
  EXPECT_EQ(r.log.calls.back().vector, 0x21);
}

TEST(dos_dispatch, the_date_time_setters_are_deferred) {
  rig r;
  r.call(ah(0x2B));
  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_service);
}

// --- Ctrl-Break, the DOS half -------------------------------------------

TEST(dos_break, a_console_call_detours_through_a_hooked_int23h) {
  rig r;

  //   0000  CD 21              INT 21h                  ; AH=02h DL='X'
  //   0002  F4                 HLT
  //   0003  45                 INC BP                   ; the hook
  //   0004  CF                 IRET
  r.poke(0, {0xCD, 0x21, 0xF4, 0x45, 0xCF});

  // Hook INT 23h at code_segment:0003, the way a program's own Ctrl-Break
  // handler would.
  const std::uint32_t entry = cpu::physical_address(
      cpu::vector_table_segment, cpu::vector_table_offset(0x23));
  const std::span<std::uint8_t> ram = r.pc().memory().ram();
  ram[entry] = 0x03;
  ram[entry + 1] = 0x00;
  ram[entry + 2] = static_cast<std::uint8_t>(code_segment);
  ram[entry + 3] = static_cast<std::uint8_t>(code_segment >> 8u);

  // The BIOS break flag, set as if #53 had just seen Ctrl-Break.
  r.pc().memory().ram()[cpu::physical_address(bda::segment, bda::break_flag)] =
      bda::break_flag_bit;

  r.box->processor().reset();
  cpu::registers& regs = r.regs();
  regs[cpu::sreg::cs] = code_segment;
  regs[cpu::sreg::ss] = code_segment;
  regs[cpu::reg16::sp] = stack_top;
  regs.ip = 0;
  regs[cpu::reg16::ax] = ah(0x02);
  regs[cpu::reg16::dx] = static_cast<std::uint8_t>('X');
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());

  // The hook ran...
  EXPECT_EQ(r.regs()[cpu::reg16::bp], 1u);
  // ...the flag is clear again...
  EXPECT_EQ(r.pc().memory().ram()[cpu::physical_address(bda::segment,
                                                        bda::break_flag)],
            0);
  // ...and the console call it detoured never ran: no byte reached the
  // sink, and the machine halted on the HLT right after the original
  // INT 21h rather than stopping or looping — the original call's own
  // return address, not the hook's.
  std::array<std::uint8_t, 4> drained{};
  EXPECT_EQ(r.pc().console().read(drained), 0u);
  EXPECT_FALSE(r.pc().stopped());
}

}  // namespace
}  // namespace amberfolio::machine
