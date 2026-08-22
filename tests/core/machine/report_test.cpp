// SPDX-License-Identifier: AGPL-3.0-only
//
// The stop report: the exact sentence the machine says when a run ends.
//
// This is a test of a *format*, which is unusual enough to say why it is
// worth having. M3's method (#94) is to read the line and widen the one
// thing it names, and M3's exit criterion is that the desktop host and
// the browser report the same line at the same step (#84). Both of those
// rest on the line being a fixed shape rather than an approximate one, so
// the shape is asserted here — field by field, and by whole line where a
// whole line is the claim.
//
// The cases are chosen for what a first boot actually produces: a service
// this machine does not have, an opcode it will not invent, a program
// that exited on purpose, and a run that was cut short by a budget.

#include "amberfolio/machine/report.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/vfs.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x1000;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {}

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  void program(std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[at] = byte;
      ++at;
    }
    box->processor().reset();
    box->processor().regs()[cpu::sreg::cs] = code_segment;
    box->processor().regs().ip = 0;
    box->processor().regs()[cpu::sreg::ss] = code_segment;
    box->processor().regs()[cpu::reg16::sp] = 0xFFFE;
  }

  /// Step until the machine stops or `limit` steps have gone by — the
  /// harness's own budget, so a test that fails to stop fails rather than
  /// hangs.
  void run_until_stopped(unsigned limit = 200) const {
    for (unsigned i = 0; i < limit && !box->stopped(); ++i) {
      box->step();
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

[[nodiscard]] std::string stop_text(const machine& box, run_end how) {
  std::array<char, stop_report_capacity> buffer{};
  const std::size_t n = format_stop_report(box, how, buffer);
  return {buffer.data(), n};
}

[[nodiscard]] std::string trace_text(const machine& box) {
  std::vector<char> buffer(trace_report_capacity);
  const std::size_t n = format_trace_report(box, buffer);
  return {buffer.data(), n};
}

/// Whether `haystack` contains `needle`. Spelled out because the whole
/// point of these cases is the exact characters.
[[nodiscard]] bool has(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

// --- Names -------------------------------------------------------------

TEST(ReportNames, SpellEveryStopReasonAsItsEnumerator) {
  EXPECT_STREQ(stop_reason_name(stop_reason::none), "none");
  EXPECT_STREQ(stop_reason_name(stop_reason::processor), "processor");
  EXPECT_STREQ(stop_reason_name(stop_reason::unimplemented_service),
               "unimplemented_service");
  EXPECT_STREQ(stop_reason_name(stop_reason::conflicting_claim),
               "conflicting_claim");
  EXPECT_STREQ(stop_reason_name(stop_reason::unimplemented_device),
               "unimplemented_device");
  EXPECT_STREQ(stop_reason_name(stop_reason::program_exited), "program_exited");
  EXPECT_STREQ(stop_reason_name(stop_reason::unsupported_request),
               "unsupported_request");
}

TEST(ReportNames, SpellEveryRunEnd) {
  EXPECT_STREQ(run_end_name(run_end::stopped), "stopped");
  EXPECT_STREQ(run_end_name(run_end::step_budget), "step_budget");
  EXPECT_STREQ(run_end_name(run_end::tick_budget), "tick_budget");
  EXPECT_STREQ(run_end_name(run_end::host_quit), "host_quit");
}

TEST(ReportNames, SpellEveryNoticeKind) {
  EXPECT_STREQ(notice_kind_name(notice_kind::unmapped_memory_read),
               "unmapped_memory_read");
  EXPECT_STREQ(notice_kind_name(notice_kind::unmapped_memory_write),
               "unmapped_memory_write");
  EXPECT_STREQ(notice_kind_name(notice_kind::rom_write), "rom_write");
  EXPECT_STREQ(notice_kind_name(notice_kind::unclaimed_port_read),
               "unclaimed_port_read");
  EXPECT_STREQ(notice_kind_name(notice_kind::unclaimed_port_write),
               "unclaimed_port_write");
  EXPECT_STREQ(notice_kind_name(notice_kind::video_write_before_mode_set),
               "video_write_before_mode_set");
}

// --- The step counter the report is read against -----------------------

TEST(MachineSteps, CountEveryStepAndStartAgainAtReset) {
  const rig r;
  r.program({0xEB, 0xFE});  // JMP $ — one step, forever.

  EXPECT_EQ(r.pc().steps(), 0u);
  for (unsigned i = 0; i < 5; ++i) {
    r.pc().step();
  }
  EXPECT_EQ(r.pc().steps(), 5u);

  r.pc().reset();
  EXPECT_EQ(r.pc().steps(), 0u);
}

TEST(MachineSteps, AgreeWithTheTicksARunConsumed) {
  const rig r;
  r.program({0xEB, 0xFE});

  const run_result done = r.pc().run(4000);
  EXPECT_EQ(r.pc().steps(), done.steps);
  EXPECT_EQ(r.pc().time(),
            done.steps * r.pc().step_cost_subticks() / subticks_per_tick);
}

// --- The report --------------------------------------------------------

TEST(StopReport, NamesTheServiceToWidenNext) {
  const rig r;
  // INT 21h on a machine with no DOS layer installed: the vector's stub
  // has no handler, so the floor refuses it (PLAN.md §3) and this is the
  // shape of every M3 worklist line.
  r.program({0xCD, 0x21});
  r.run_until_stopped();
  ASSERT_TRUE(r.pc().stopped());

  const std::string text = stop_text(r.pc(), run_end::stopped);

  EXPECT_TRUE(has(text, "amberfolio: stop reason=unimplemented_service "))
      << text;
  EXPECT_TRUE(has(text, "amberfolio: stop call=INT21 ah=00 al=00 ax=0000"))
      << text;
  EXPECT_TRUE(has(text, "outcome=unimplemented")) << text;
  EXPECT_TRUE(has(text, "amberfolio: stop next=INT 21h AH=00h AL=00h")) << text;
  // The caller, read off its own stack rather than guessed: the
  // instruction after the INT, which is offset 2 in this program.
  EXPECT_TRUE(has(text, "from=1000:0002")) << text;
}

TEST(StopReport, PassesThroughWhatTheProcessorRefused) {
  const rig r;
  // An unbroken run of segment-override prefixes: the one thing the
  // interpreter knowingly declines to do (cpu/diagnostics.h's
  // `prefix_chain_too_long`), and therefore the only way a machine
  // running the real instruction set can be made to stop for a
  // processor reason at all — every opcode has a handler.
  {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (unsigned i = 0; i <= cpu::processor::prefix_limit; ++i) {
      r.pc().memory().ram()[at + i] = 0x26;  // ES:
    }
    r.pc().processor().reset();
    r.pc().processor().regs()[cpu::sreg::cs] = code_segment;
    r.pc().processor().regs().ip = 0;
  }
  r.run_until_stopped();
  ASSERT_TRUE(r.pc().stopped());

  const std::string text = stop_text(r.pc(), run_end::stopped);
  EXPECT_TRUE(has(text, "reason=processor ")) << text;
  EXPECT_TRUE(has(text, "cpu=prefix_chain_too_long opcode=26")) << text;
  EXPECT_TRUE(has(text, "amberfolio: stop next=opcode 26")) << text;
}

TEST(StopReport, ReportsAnExitAsAnExitAndOffersNoWorklistLine) {
  const rig r;
  r.program({0xEB, 0xFE});
  r.pc().step();
  r.pc().exit_program(90);

  const std::string text = stop_text(r.pc(), run_end::stopped);
  EXPECT_TRUE(has(text, "reason=program_exited ")) << text;
  EXPECT_TRUE(has(text, "amberfolio: stop exit=90")) << text;
  EXPECT_FALSE(has(text, "next=")) << "a program that exited asked for "
                                      "nothing:\n"
                                   << text;
}

TEST(StopReport, ReportsTheHostsOwnReasonWhenTheMachineDidNotStop) {
  const rig r;
  r.program({0xEB, 0xFE});
  r.pc().run(4000);
  ASSERT_FALSE(r.pc().stopped());

  const std::string text = stop_text(r.pc(), run_end::step_budget);
  EXPECT_TRUE(has(text, "reason=step_budget ")) << text;
  // And where it was when the budget ran out, which is the whole value of
  // turning a hang into a report.
  EXPECT_TRUE(has(text, "cs=1000 ip=0000")) << text;
  EXPECT_TRUE(has(text, "steps=1000 ")) << text;
}

TEST(StopReport, IsOneLinePerFactAndEveryLineCarriesThePrefix) {
  const rig r;
  r.program({0xCD, 0x21});
  r.run_until_stopped();

  const std::string text = stop_text(r.pc(), run_end::stopped);
  ASSERT_FALSE(text.empty());
  EXPECT_EQ(text.back(), '\n');

  std::size_t at = 0;
  unsigned lines = 0;
  while (at < text.size()) {
    EXPECT_EQ(text.compare(at, 17, "amberfolio: stop "), 0)
        << "line " << lines << " of:\n"
        << text;
    at = text.find('\n', at) + 1;
    ++lines;
  }
  EXPECT_EQ(lines, 3u) << text;
}

TEST(StopReport, TruncatesRatherThanRefusingASmallBuffer) {
  const rig r;
  r.program({0xCD, 0x21});
  r.run_until_stopped();

  std::array<char, 24> small{};
  const std::size_t n = format_stop_report(r.pc(), run_end::stopped, small);
  EXPECT_EQ(n, small.size() - 1);
  EXPECT_EQ(small[small.size() - 1], '\0');
}

// --- The trace report ---------------------------------------------------

TEST(TraceReport, SaysSoWhenNothingWasRecorded) {
  const rig r;
  EXPECT_EQ(trace_text(r.pc()), "amberfolio: stop trace=off\n");
}

TEST(TraceReport, NumbersEachStepByTheStepItActuallyWas) {
  const rig r;
  r.pc().trace().enable(true);
  r.program({0xEB, 0xFE});
  for (unsigned i = 0; i < 4; ++i) {
    r.pc().step();
  }

  const std::string text = trace_text(r.pc());
  EXPECT_TRUE(has(text, "trace=on steps_seen=4 kept=4")) << text;
  EXPECT_TRUE(has(text, "amberfolio: stop trace step=0 at=1000:0000")) << text;
  EXPECT_TRUE(has(text, "amberfolio: stop trace step=3 at=1000:0000")) << text;
}

TEST(TraceReport, ListsTheServiceCallsTheProgramMade) {
  const rig r;
  r.pc().trace().enable(true);
  r.program({0xCD, 0x21});
  r.run_until_stopped();

  const std::string text = trace_text(r.pc());
  EXPECT_TRUE(has(text, "trace call=INT21 ax=0000 from=1000:0002")) << text;
}

TEST(TraceReport, SurvivesResetAsASettingAndForgetsWhatItHeld) {
  const rig r;
  r.pc().trace().enable(true);
  r.program({0xEB, 0xFE});
  r.pc().step();
  ASSERT_EQ(r.pc().trace().steps_seen(), 1u);

  r.pc().reset();

  EXPECT_TRUE(r.pc().trace().enabled());
  EXPECT_EQ(r.pc().trace().steps_seen(), 0u);
}

// --- Naming a file (M4-G3/#104, M4-G4/#105) -----------------------------
//
// The file-activity channel's rendering: a path back into text, and the
// two enumerators a log line spells out. Here rather than in dos_test.cpp
// because these are the format, and dos_test.cpp is what checks that the
// events reach a sink at all.

/// `format_dos_path` into a string, with room to spare.
[[nodiscard]] std::string path_text(const dos_path& path) {
  std::array<char, dos_path_capacity> out{};
  const std::size_t wrote = format_dos_path(path, out);
  EXPECT_LT(wrote, out.size());
  return {out.data()};
}

[[nodiscard]] dos_path path_of(std::initializer_list<std::string_view> parts) {
  dos_path path;
  for (const std::string_view part : parts) {
    const vfs_result<dos_name> name =
        dos_name::parse(std::span<const char>(part.data(), part.size()));
    EXPECT_TRUE(name.ok());
    EXPECT_TRUE(path.push(name.value));
  }
  return path;
}

TEST(FilePath, RendersALeafUnderTheRoot) {
  EXPECT_EQ(path_text(path_of({"GAME.DAT"})), "\\GAME.DAT");
}

TEST(FilePath, RendersEveryComponentSeparated) {
  EXPECT_EQ(path_text(path_of({"SAVE", "SLOT01", "CHAR1.DAT"})),
            "\\SAVE\\SLOT01\\CHAR1.DAT");
}

TEST(FilePath, RendersTheRootAsALoneSeparator) {
  EXPECT_EQ(path_text(dos_path{}), "\\");
}

TEST(FilePath, TruncatesRatherThanRefusingASmallBuffer) {
  // The two report writers' rule, kept here: the caller is told what it
  // would have needed, and what fits is still terminated.
  const dos_path path = path_of({"SAVE", "SLOT01"});
  std::array<char, 8> out{};
  const std::size_t wrote = format_dos_path(path, out);
  EXPECT_EQ(wrote, std::string_view("\\SAVE\\SLOT01").size());
  EXPECT_STREQ(out.data(), "\\SAVE\\S");
}

TEST(FileNames, SpellEachActionAndErrorAsItsOwnEnumerator) {
  EXPECT_STREQ(file_action_name(file_action::open), "open");
  EXPECT_STREQ(file_action_name(file_action::create), "create");
  EXPECT_STREQ(file_action_name(file_action::mkdir), "mkdir");
  EXPECT_STREQ(file_action_name(file_action::unlink), "unlink");
  EXPECT_STREQ(file_action_name(file_action::close), "close");

  EXPECT_STREQ(vfs_error_name(vfs_error::none), "none");
  EXPECT_STREQ(vfs_error_name(vfs_error::file_not_found), "file_not_found");
  EXPECT_STREQ(vfs_error_name(vfs_error::access_denied), "access_denied");
  EXPECT_STREQ(vfs_error_name(vfs_error::directory_full), "directory_full");
}

}  // namespace
}  // namespace amberfolio::machine
