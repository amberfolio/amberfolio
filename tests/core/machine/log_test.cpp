// SPDX-License-Identifier: AGPL-3.0-only
//
// The account kept for a host that cannot be handed records (#108).
//
// `report_test.cpp` asserts the *lines*; this asserts the ring that holds
// them — what it keeps, what it drops, and the two channels it keeps only
// when asked. The thing worth testing here is not that a string arrives:
// it is that the ring behaves the way its header says under the two
// conditions a host will actually meet, a partial drain and an overflow,
// because both of those are silent when they are wrong.

#include "amberfolio/machine/log.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/seam.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// Drain the whole log into a string, the way a host that keeps up does.
[[nodiscard]] std::string drain(diagnostic_log& log) {
  std::string text;
  std::array<char, diagnostic_log::capacity> out{};
  const std::size_t got = log.read(out);
  text.assign(out.data(), got);
  return text;
}

[[nodiscard]] notice a_notice(std::uint32_t at) {
  return notice{.what = notice_kind::rom_write,
                .at = at,
                .value = 0xAB,
                .cs = 0x1000,
                .ip = 0x0002};
}

TEST(DiagnosticLog, IsEmptyUntilSomethingIsReported) {
  diagnostic_log log;
  EXPECT_EQ(log.pending(), 0u);
  EXPECT_EQ(log.dropped(), 0u);
  EXPECT_EQ(drain(log), "");
}

TEST(DiagnosticLog, KeepsTheLineReportHWouldHaveWritten) {
  diagnostic_log log;
  log.report(a_notice(0xF0147));
  EXPECT_EQ(drain(log),
            "amberfolio: notice rom_write at F0147 value=AB from=1000:0002\n");
}

TEST(DiagnosticLog, KeepsLinesInTheOrderTheyHappened) {
  diagnostic_log log;
  log.report(a_notice(0x00001));
  log.report(a_notice(0x00002));
  const std::string text = drain(log);
  EXPECT_NE(text.find("at 00001"), std::string::npos);
  EXPECT_NE(text.find("at 00002"), std::string::npos);
  EXPECT_LT(text.find("at 00001"), text.find("at 00002"));
}

TEST(DiagnosticLog, LosesNothingToAShortDrain) {
  // A host whose buffer is smaller than what is waiting gets the rest on
  // its next call, so what it appends is still whole lines. This is the
  // case the dev page meets every frame and it is invisible when wrong.
  diagnostic_log log;
  log.report(a_notice(0x00001));
  log.report(a_notice(0x00002));
  const std::size_t whole = log.pending();

  std::string text;
  std::array<char, 7> sip{};
  for (std::size_t got = log.read(sip); got != 0; got = log.read(sip)) {
    text.append(sip.data(), got);
  }
  EXPECT_EQ(text.size(), whole);
  EXPECT_EQ(log.pending(), 0u);
  EXPECT_NE(text.find("at 00001"), std::string::npos);
  EXPECT_NE(text.find("at 00002"), std::string::npos);
}

TEST(DiagnosticLog, DropsAWholeLineRatherThanWritingPartOfOne) {
  // Half a line in a log reads as a fact, so a line that will not fit is
  // not written at all — and the count says how many were not.
  diagnostic_log log;
  std::uint64_t reported = 0;
  while (log.dropped() == 0) {
    log.report(a_notice(0x00001));
    ++reported;
    ASSERT_LT(reported, 10000u) << "the ring never filled";
  }
  const std::string text = drain(log);
  EXPECT_LE(text.size(), diagnostic_log::capacity);
  EXPECT_EQ(text.back(), '\n');
  // Every line whole: as many newlines as lines, and nothing after the
  // last one.
  const std::size_t lines =
      static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
  const std::size_t one =
      std::string_view(
          "amberfolio: notice rom_write at 00001 value=AB from=1000:0002\n")
          .size();
  EXPECT_EQ(text.size(), lines * one);
}

TEST(DiagnosticLog, KeepsTheDropCountAcrossADrain) {
  // It is a property of the run, not of the buffer: a host that wants to
  // say "the log was truncated" needs the total.
  diagnostic_log log;
  while (log.dropped() == 0) {
    log.report(a_notice(0x00001));
  }
  const std::uint64_t lost = log.dropped();
  EXPECT_EQ(drain(log).empty(), false);
  EXPECT_EQ(log.dropped(), lost);
  EXPECT_EQ(log.pending(), 0u);
}

TEST(DiagnosticLog, TakesTheHighVolumeChannelsOnlyWhenAsked) {
  // A boot makes tens of thousands of service calls. Off by default, on
  // together, exactly as `--trace` does on the desktop host.
  const service_call call{.vector = 0x21,
                          .ax = 0x3D00,
                          .caller_cs = 0x0B58,
                          .caller_ip = 0x0052,
                          .outcome = service_outcome::handled};
  const file_event event{.what = file_action::close,
                         .path = dos_path{},
                         .handle = 5,
                         .error = vfs_error::none,
                         .caller_cs = 0x0B58,
                         .caller_ip = 0x0052};

  diagnostic_log log;
  EXPECT_FALSE(log.tracing());
  log.report(call);
  log.report(event);
  EXPECT_EQ(log.pending(), 0u);

  log.set_tracing(true);
  log.report(call);
  log.report(event);
  const std::string text = drain(log);
  EXPECT_NE(text.find("call INT21"), std::string::npos);
  EXPECT_NE(text.find("file close"), std::string::npos);
}

TEST(DiagnosticLog, KeepsEverySeamTransitionWhetherTracingOrNot) {
  // The one kind of line a boot log must never lose: it is the difference
  // between a plain machine and an enhanced one (seam.h).
  diagnostic_log log;
  log.report(seam_event{.id = "cheat-kill-all",
                        .kind = seam_event_kind::armed,
                        .reason = seam_reason::none});
  EXPECT_EQ(drain(log), "amberfolio: seam cheat-kill-all armed\n");
}

TEST(DiagnosticLog, SaysNothingAboutAProgramThatExited) {
  diagnostic_log log;
  log.report(stop_record{
      .reason = stop_reason::program_exited, .at = 0, .exit_code = 0});
  EXPECT_EQ(log.pending(), 0u);
  EXPECT_EQ(log.dropped(), 0u);
}

TEST(DiagnosticLog, ForgetsEverythingWhenTheHostSaysSo) {
  diagnostic_log log;
  while (log.dropped() == 0) {
    log.report(a_notice(0x00001));
  }
  log.clear();
  EXPECT_EQ(log.pending(), 0u);
  EXPECT_EQ(log.dropped(), 0u);
  EXPECT_EQ(drain(log), "");
}

}  // namespace
}  // namespace amberfolio::machine
