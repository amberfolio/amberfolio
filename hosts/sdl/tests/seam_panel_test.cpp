// SPDX-License-Identifier: AGPL-3.0-only
//
// The toggle panel's columns (#383).
//
// The claim under test is the issue's own: **five facts on every row**,
// and `fired` among them as a number rather than a tick. A seam that
// armed and fired nothing reads exactly like one that worked (#131,
// #163), and a panel that dropped the count — or lined it up somewhere
// different on every run — would be the surface that hides the failure
// it exists to show.
//
// Beside that, the two things a reader cannot check by eye: that a value
// too long for its column stops at the column and does not walk into the
// one beside it, and that a click lands on the row it looks like it
// landed on.

#include "seam_panel.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

/// A status row with nothing wrong with it, for a test to spoil one
/// field of.
[[nodiscard]] machine::seam_status a_status() {
  return machine::seam_status{.id = "automap",
                              .about = "a panel of where the party has been",
                              .state = machine::seam_state::on,
                              .reason = machine::seam_reason::none,
                              .armed = true,
                              .fired = 12,
                              .addressed = true};
}

/// Which line of `panel_lines()` seam `index` is on.
[[nodiscard]] std::string row_line(const std::vector<std::string>& lines,
                                   std::size_t index) {
  // By value: every caller below hands this a temporary vector, and a
  // reference into one is a reference into a string that is already gone.
  return lines.at(panel_first_row + index);
}

TEST(SeamPanel, PutsTheFiveFactsOnOneRow) {
  const panel_row row = panel_row_of(a_status(), machine::document_kind::none);
  EXPECT_EQ(row.id, "automap");
  EXPECT_EQ(row.state, "on armed");
  EXPECT_EQ(row.fired, 12U);
  EXPECT_EQ(row.reason, "-");
  EXPECT_EQ(row.gate, "-");

  const std::string line = row_line(panel_lines({row}, 0), 0);
  EXPECT_NE(line.find("automap"), std::string::npos);
  EXPECT_NE(line.find("on armed"), std::string::npos);
  // The number, and not a tick: this is the whole of #131's lesson on a
  // row a person looks at.
  EXPECT_NE(line.find("12"), std::string::npos);
}

TEST(SeamPanel, SaysZeroForAnArmedSeamThatFiredNothing) {
  machine::seam_status status = a_status();
  status.fired = 0;
  const std::vector<std::string> lines =
      panel_lines({panel_row_of(status, machine::document_kind::none)}, 0);
  EXPECT_NE(row_line(lines, 0).find('0'), std::string::npos);
  // And the sentence core works out about that pair (#163) is under the
  // table rather than being re-derived here.
  EXPECT_NE(lines.back().find("reached"), std::string::npos);
}

TEST(SeamPanel, KeepsCoresWordForARefusal) {
  machine::seam_status status = a_status();
  status.state = machine::seam_state::on;
  status.armed = false;
  status.reason = machine::seam_reason::document_not_presented;
  const panel_row row = panel_row_of(status, machine::document_kind::journal);
  EXPECT_EQ(row.state, "on inert");
  // Core's word, never this host's paraphrase: the part a player can act
  // on is the reason and the document it names.
  EXPECT_EQ(row.reason, "document_not_presented");
  EXPECT_EQ(row.gate, "journal");
  const std::string line = row_line(panel_lines({row}, 0), 0);
  EXPECT_NE(line.find("document_not_presented"), std::string::npos);
  EXPECT_NE(line.find("journal"), std::string::npos);
}

TEST(SeamPanel, MarksAnUnavailableSeamAsOneNobodyCanTick) {
  machine::seam_status status = a_status();
  status.state = machine::seam_state::unavailable;
  status.reason = machine::seam_reason::wrong_binary;
  status.armed = false;
  const panel_row row = panel_row_of(status, machine::document_kind::none);
  EXPECT_FALSE(row.available);
  EXPECT_FALSE(row.on);
  const std::string line = row_line(panel_lines({row}, 0), 0);
  EXPECT_EQ(line.find("[ ]"), std::string::npos);
  EXPECT_EQ(line.find("[x]"), std::string::npos);
  EXPECT_NE(line.find("wrong_binary"), std::string::npos);
}

TEST(SeamPanel, TicksTheBoxOfASeamThatIsOnAndNotOfOneThatIsOff) {
  machine::seam_status on = a_status();
  machine::seam_status off = a_status();
  off.state = machine::seam_state::off;
  off.armed = false;
  const std::vector<std::string> lines =
      panel_lines({panel_row_of(on, machine::document_kind::none),
                   panel_row_of(off, machine::document_kind::none)},
                  0);
  EXPECT_TRUE(row_line(lines, 0).starts_with("[x] "));
  EXPECT_TRUE(row_line(lines, 1).starts_with("[ ] "));
}

TEST(SeamPanel, KeepsEveryValueInsideItsOwnColumn) {
  // The longest id, state, reason and gate core can produce, all on one
  // row: the columns are sized for exactly this, and a value that walked
  // into the column beside it would put a reason where a state goes.
  machine::seam_status status = a_status();
  status.id = "cheat-invulnerable";
  status.state = machine::seam_state::unavailable;
  status.reason = machine::seam_reason::document_not_presented;
  status.armed = false;
  const std::string line = row_line(
      panel_lines({panel_row_of(status, machine::document_kind::code_wheel)},
                  0),
      0);
  EXPECT_NE(line.find("cheat-invulnerable"), std::string::npos);
  EXPECT_NE(line.find("unavailable"), std::string::npos);
  EXPECT_NE(line.find("document_not_presented"), std::string::npos);
  EXPECT_NE(line.find("code wheel"), std::string::npos);
  // A space between every pair of them, which is what "inside its own
  // column" means when the columns are fixed.
  EXPECT_EQ(line.find("cheat-invulnerableunavailable"), std::string::npos);
  EXPECT_EQ(line.find("unavailabledocument"), std::string::npos);
}

TEST(SeamPanel, LinesTheFiredColumnUpUnderItsHeading) {
  machine::seam_status few = a_status();
  few.fired = 3;
  machine::seam_status many = a_status();
  many.fired = 40125;
  const std::vector<std::string> lines =
      panel_lines({panel_row_of(few, machine::document_kind::none),
                   panel_row_of(many, machine::document_kind::none)},
                  0);
  // Right-aligned, so the last digit of every count is in one place and
  // a zero among five-figure numbers is still where a reader looks.
  const std::size_t heading = lines.at(1).find("fired");
  ASSERT_NE(heading, std::string::npos);
  const std::size_t last = heading + 4;
  EXPECT_EQ(row_line(lines, 0).at(last), '3');
  EXPECT_EQ(row_line(lines, 1).at(last), '5');
}

TEST(SeamPanel, SaysWhatTheFocusedSeamIsFor) {
  machine::seam_status one = a_status();
  machine::seam_status two = a_status();
  two.id = "journal";
  two.about = "read your own journal in the game";
  const std::vector<panel_row> rows{
      panel_row_of(one, machine::document_kind::none),
      panel_row_of(two, machine::document_kind::none)};
  EXPECT_NE(panel_lines(rows, 1)
                .at(panel_first_row + 2)
                .find("read your own journal"),
            std::string::npos);
  // And a focus on no row says so rather than describing whichever row
  // an out-of-range index happened to land on.
  EXPECT_NE(panel_lines(rows, panel_no_row)
                .at(panel_first_row + 2)
                .find("no row picked"),
            std::string::npos);
}

TEST(SeamPanel, FitsInsideTheWindowItIsGiven) {
  constexpr std::array<std::pair<int, int>, 4> windows{{
      {960, 600},
      {320, 200},
      {1920, 480},
      {600, 1200},
  }};
  const std::vector<std::string> lines =
      panel_lines({panel_row_of(a_status(), machine::document_kind::none)}, 0);
  for (const auto& [width, height] : windows) {
    SCOPED_TRACE(std::to_string(width) + "x" + std::to_string(height));
    const panel_box box = fit_panel(lines, width, height);
    EXPECT_GE(box.scale, 1.0F);
    EXPECT_LE(box.height, static_cast<float>(height));
  }
}

TEST(SeamPanel, FindsTheRowAPointerLandedOn) {
  std::vector<panel_row> rows;
  for (int i = 0; i < 6; ++i) {
    machine::seam_status status = a_status();
    status.id = "seam";
    rows.push_back(panel_row_of(status, machine::document_kind::none));
  }
  const std::vector<std::string> lines = panel_lines(rows, 0);
  const panel_box box = fit_panel(lines, 1920, 1200);
  const float line_height = box.scale * 8.0F;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const float middle =
        box.top +
        (line_height * (static_cast<float>(i + panel_first_row) + 0.5F));
    EXPECT_EQ(row_under(box, rows.size(), box.left + 1.0F, middle), i);
  }
  // The title, the column header and the description under the table are
  // not rows, and neither is anywhere outside the panel.
  EXPECT_EQ(row_under(box, rows.size(), box.left + 1.0F, box.top + 1.0F),
            panel_no_row);
  EXPECT_EQ(
      row_under(box, rows.size(), box.left + 1.0F, box.top + box.height - 1.0F),
      panel_no_row);
  EXPECT_EQ(row_under(box, rows.size(), box.left - 4.0F, box.top + line_height),
            panel_no_row);
}

}  // namespace
}  // namespace amberfolio::sdl
