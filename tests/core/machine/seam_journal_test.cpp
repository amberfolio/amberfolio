// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal reader (seam_journal.cpp, M5-E4 #175), exercised through
// its mechanism and not through any program.
//
// Three halves, which is what #175 asks for. The **citation recognizer**
// reads nothing and is checked against strings this file writes. The
// **reader** is checked by laying a data segment and a font out the way
// the facts say the program lays them out, standing the machine on an
// interception point, handing the seam some text through a stand-in host,
// and looking at the panel and at the planes. The **fidelity pair** is
// the seam on with nothing cited and no key pressed, against the same run
// with it off.
//
// The offsets below are restated rather than read out of the seam, which
// is `seam_cheats_test.cpp`'s rule and the reason for it: a test that took
// its layout from the code it is checking would be agreeing with itself.
// The interception addresses *are* read from the definition, because those
// are the mechanism rather than the layout.
//
// **Every byte of text here is this file's own.** Nothing in it is a
// journal, resembles a journal, or is taken from any document
// (CONTRIBUTING.md, `docs/journal.md`): the entries are sentences written
// to have the shapes the wrapper and the recognizer have to cope with.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/journal.h"
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
constexpr std::uint16_t data_current_member = 0x5D92;
constexpr std::uint16_t data_key_pushback = 0x8501;
constexpr std::uint16_t data_font_pointer = 0x5E20;

constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint8_t mode_title = 0;

/// The string drawer's frame at its entry, from the far return address
/// upwards: the string's offset, then its segment.
constexpr std::uint16_t draw_frame_string_offset = 4;
constexpr std::uint16_t draw_frame_string_segment = 6;

/// Where the test puts the things the data segment points at.
constexpr std::uint16_t font_segment = 0x8000;
constexpr std::uint16_t member_segment = 0x9000;
constexpr std::uint16_t string_segment = 0xA00;

/// The keys, as INT 16h hands them over.
constexpr std::uint16_t key_f1 = 0x3B00;
constexpr std::uint16_t key_escape = 0x011B;
constexpr std::uint16_t key_backspace = 0x0E08;
constexpr std::uint16_t key_return = 0x1C0D;
constexpr std::uint16_t key_tab = 0x0F09;
constexpr std::uint16_t key_one = 0x0231;
constexpr std::uint16_t key_two = 0x0332;
constexpr std::uint16_t key_four = 0x0534;
constexpr std::uint16_t key_step_down = 0x5032;
/// What the seam answers the program's blocking read with when it has
/// taken the key that read was going to be handed: `-`, which is on none
/// of the program's bars.
constexpr std::uint16_t key_ignored = 0x0C2D;

/// The panel's layout, restated: a title row, twelve rows of body, and a
/// footer, eight pixels each, twenty-two glyphs across.
constexpr int glyph_height = 8;
constexpr int reader_columns = 22;
constexpr int reader_title_y = 0;
constexpr int reader_body_y = 8;
constexpr int reader_footer_y = 104;

/// A five-byte program that does nothing but let steps happen:
/// `MOV AX,1111h ; NOP ; HLT`. The seam's points are reached because the
/// test puts CS:IP on them, not because this program goes there.
constexpr std::array<std::uint8_t, 5> idle_program{0xB8, 0x11, 0x11, 0x90,
                                                   0xF4};

/// A host that answers `journal_open` out of one entry it was handed.
///
/// Deliberately not `host::host_services` — that object lives above the
/// core/host boundary and this suite is core's. What is under test here is
/// the seam and the buffer it reads, so the stand-in is the smallest thing
/// that fills the buffer, and the real object's own answers are asserted
/// where it lives (`hosts/common/tests/host_services_test.cpp`).
class one_entry_host final : public seam_host_services {
 public:
  void serve(machine& box, seam_host_service which,
             std::uint32_t argument) override {
    if (which != seam_host_service::journal_open) {
      return;
    }
    ++calls;
    asked = argument;
    if (empty) {
      box.journal().refuse(journal_delivery::no_journal);
      return;
    }
    if (argument != journal_open_argument(holds)) {
      box.journal().refuse(journal_delivery::no_entry);
      return;
    }
    box.journal().deliver(text);
  }

  /// What this host holds: one item, or nothing at all.
  ///
  /// A *citation* since #218 and not a number, because the argument that
  /// arrives is a packed pair — so a host that answered on the number
  /// alone would hand a tale the entry with the same number, which is
  /// exactly the mistake the pair exists to make impossible.
  bool empty{false};
  journal_citation holds{.kind = journal_kind::entry, .number = 12};
  std::string text;

  unsigned calls{0};
  std::uint32_t asked{0};
};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] journal_state& reader() const noexcept {
    return box->journal();
  }

  /// The program's data segment, where the seam insists on finding it.
  [[nodiscard]] static std::uint16_t dgroup() noexcept {
    return static_cast<std::uint16_t>(image_load_segment +
                                      (dgroup_offset / 16));
  }

  void enable() const {
    ASSERT_EQ(box->seams().enable("journal"), seam_reason::none);
  }

  /// The physical address one of the seam's points is armed at, by index
  /// into the definition — read from the definition, because that is the
  /// mechanism under test.
  [[nodiscard]] std::uint32_t point(std::size_t which) const {
    const seam_definition* found = box->seams().find("journal");
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
  /// Where a test pretends the overlay manager has put the adventuring
  /// module. Any segment clear of the image and the stack will do; the
  /// engine only cares that the word the module's facts name holds it.
  static constexpr std::uint16_t adventure_segment = 0x6000;

  /// Write what the program's overlay manager writes: the segment the
  /// adventuring module begins at right now. Zero is "not loaded", and is
  /// what a machine that has never been there reads.
  void manager_says_adventure_at(std::uint16_t segment) const {
    const seam_definition* journal = box->seams().find("journal");
    ASSERT_NE(journal, nullptr);
    for (const seam_point& point : journal->points) {
      if (point.module.has_load_segment()) {
        put_word(image_load_segment,
                 static_cast<std::uint16_t>(point.module.load_segment_at),
                 segment);
        return;
      }
    }
    FAIL() << "no point of this seam names an overlaid module";
  }

  /// Stand on a point inside the adventuring module, with the registers
  /// the loop has there: the data segment, its own frame, and — where the
  /// menu-bar routine returns — the letter it answered with in AL.
  void stand_in_adventure(std::uint16_t offset, std::uint8_t al = 0) const {
    manager_says_adventure_at(adventure_segment);
    box->memory().ram()[cpu::physical_address(adventure_segment, offset)] =
        0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = adventure_segment;
    r.ip = offset;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = 0x0700;
    r[cpu::reg16::bp] = 0x0600;
    r.set(cpu::reg8::al, al);
  }

  /// The bar as the program keeps it: a Pascal string at `offset`.
  void put_bar(std::uint16_t offset, std::string_view text) const {
    put_byte(dgroup(), offset, static_cast<std::uint8_t>(text.size()));
    for (std::size_t i = 0; i < text.size(); ++i) {
      put_byte(dgroup(), static_cast<std::uint16_t>(offset + 1 + i),
               static_cast<std::uint8_t>(text[i]));
    }
  }

  [[nodiscard]] std::string bar_at(std::uint16_t offset) const {
    const std::uint8_t length = byte_at(dgroup(), offset);
    std::string out;
    for (unsigned i = 1; i <= length; ++i) {
      out.push_back(static_cast<char>(
          byte_at(dgroup(), static_cast<std::uint16_t>(offset + i))));
    }
    return out;
  }

  /// One whole pass of the adventuring menu loop: the bar goes out, the
  /// routine answers with `letter`, and the bar comes back.
  void one_bar_pass(std::uint16_t before, std::uint16_t after,
                    std::uint8_t letter, std::uint8_t out_flag = 0) const {
    stand_in_adventure(before);
    box->step();
    stand_in_adventure(after, letter);
    put_byte(dgroup(), static_cast<std::uint16_t>(0x0600 - 0x04), out_flag);
    box->step();
  }

  void stand_on(std::uint32_t address, std::uint16_t sp = 0x0400) const {
    box->memory().ram()[address] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = static_cast<std::uint16_t>(
        address - cpu::physical_address(image_load_segment, 0));
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = sp;
  }

  /// A screen with a party roster on it — the only kind this reader draws
  /// over — with a font installed and nothing pushed back.
  void adventuring() const {
    const std::uint16_t ds = dgroup();
    put_byte(ds, data_game_mode, mode_adventure);
    put_byte(ds, data_key_pushback, 0);
    put_word(ds, data_current_member, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_current_member + 2),
             member_segment);
    install_font();
  }

  /// The program's 8x8 font, where the seam has to find it.
  ///
  /// Every row of glyph *g* is the byte *g*, so a glyph that reaches the
  /// panel says which index it was drawn from — which is the half of this
  /// that could go wrong quietly. The character-to-index mapping is the
  /// program's own (upper-cased, modulo sixty-four) and is restated by the
  /// expectations rather than shared with the code under test.
  void install_font() const {
    const std::uint16_t ds = dgroup();
    put_word(ds, data_font_pointer, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_font_pointer + 2),
             font_segment);
    for (unsigned glyph = 0; glyph < 64; ++glyph) {
      for (unsigned row = 0; row < 8; ++row) {
        put_byte(font_segment, static_cast<std::uint16_t>((glyph * 8) + row),
                 static_cast<std::uint8_t>(glyph));
      }
    }
  }

  void no_font() const {
    const std::uint16_t ds = dgroup();
    put_word(ds, data_font_pointer, 0);
    put_word(ds, static_cast<std::uint16_t>(data_font_pointer + 2), 0);
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

  /// Step the keyboard-poll point `times` times.
  void poll(unsigned times = 1) const {
    for (unsigned i = 0; i < times; ++i) {
      stand_on(point(0));
      box->step();
    }
  }

  /// The program draws `what`: stand on the string-drawer point with the
  /// far pointer where its caller's frame would have it, and step.
  ///
  /// Driven through the point rather than by calling the recognizer,
  /// because the point *is* the mechanism — the reading of that frame is
  /// what turns a string the program drew into a citation.
  void program_draws(std::string_view what,
                     std::uint16_t where = 0x0100) const {
    constexpr std::uint16_t sp = 0x0400;
    stand_on(point(2), sp);
    put_word(dgroup(),
             static_cast<std::uint16_t>(sp + draw_frame_string_offset), where);
    put_word(dgroup(),
             static_cast<std::uint16_t>(sp + draw_frame_string_segment),
             string_segment);
    put_byte(string_segment, where, static_cast<std::uint8_t>(what.size()));
    for (std::size_t i = 0; i < what.size(); ++i) {
      put_byte(string_segment, static_cast<std::uint16_t>(where + 1 + i),
               static_cast<std::uint8_t>(what[i]));
    }
    box->step();
  }

  /// One pixel of the rendered panel, before it reaches the planes.
  [[nodiscard]] std::uint8_t panel_pixel(unsigned x, unsigned y) const {
    return reader()
        .pixels()[(static_cast<std::size_t>(y) * automap_panel_width) + x];
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

  /// The glyph index the panel is showing at a character cell, read out of
  /// the test font's own rule: every row of glyph *g* is the byte *g*, so
  /// the eight pixels of a row are the bits of the index.
  [[nodiscard]] std::uint8_t glyph_at(int column, int y,
                                      std::uint8_t& colour) const {
    std::uint8_t bits = 0;
    colour = 0;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint8_t pixel = panel_pixel(
          static_cast<unsigned>((column * 8) + bit), static_cast<unsigned>(y));
      if (pixel != 0) {
        bits = static_cast<std::uint8_t>(bits | (0x80U >> bit));
        colour = pixel;
      }
    }
    return bits;
  }

  /// What a whole row of the panel says, as the characters the test font
  /// maps back to. Glyph index is the character upper-cased modulo 64, so
  /// an index under 32 is a letter and 32 is a space.
  [[nodiscard]] std::string row_text(int y) const {
    std::string out;
    for (int column = 0; column < reader_columns; ++column) {
      std::uint8_t colour = 0;
      const std::uint8_t glyph = glyph_at(column, y, colour);
      out.push_back(glyph == 0 ? ' ' : static_cast<char>(glyph + 0x40));
    }
    while (!out.empty() && out.back() == ' ') {
      out.pop_back();
    }
    return out;
  }

  /// The EGA, attached so there is something for a plane write to reach.
  void attach_video() {
    video = std::make_unique<ega>(*box);
    box->attach(*video);
  }

  /// The host, attached so that `journal_open` reaches an implementation.
  void attach_host() { box->seams().set_host(&host); }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
  one_entry_host host;
};

/// A line the reader centres, as `row_text` reads it back: blank cells
/// where nothing was drawn, then the glyphs. Centring is on a whole
/// character cell, which is the grid the program sets its own text on.
[[nodiscard]] std::string as_glyphs(std::string_view text);

/// The prompt line, which leaves one cell for the cursor it draws itself
/// (`seam_journal.cpp` says why the cursor is pixels and not a glyph).
[[nodiscard]] std::string prompt_row(std::string_view text) {
  const auto columns = static_cast<int>(text.size());
  return std::string(
             static_cast<std::size_t>((reader_columns - (columns + 1)) / 2),
             ' ') +
         as_glyphs(text);
}

[[nodiscard]] std::string centred(std::string_view text) {
  const auto columns = static_cast<int>(text.size());
  return std::string(static_cast<std::size_t>((reader_columns - columns) / 2),
                     ' ') +
         as_glyphs(text);
}

/// The test font maps a character to `upper(ch) % 64`, so the glyph a
/// letter draws is the letter minus 0x40. `row_text` undoes it.
[[nodiscard]] std::string as_glyphs(std::string_view text) {
  std::string out;
  for (const char ch : text) {
    auto code = static_cast<std::uint8_t>(ch);
    if (code >= 'a' && code <= 'z') {
      code = static_cast<std::uint8_t>(code - 0x20);
    }
    out.push_back(static_cast<char>((code % 64) + 0x40));
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// ---------------------------------------------------------------------------
// The recognizer, against strings this file writes
// ---------------------------------------------------------------------------

/// A citation, spelled out. Since #218 a citation is a *pair* — which
/// section and which number — so these tests name the section every time
/// rather than leaning on a default, which is the thing the change is
/// about.
constexpr journal_citation Entry(std::uint16_t number) {
  return {.kind = journal_kind::entry, .number = number};
}

constexpr journal_citation Tale(std::uint16_t number) {
  return {.kind = journal_kind::tale, .number = number};
}

constexpr journal_citation Proclamation(std::uint16_t number) {
  return {.kind = journal_kind::proclamation, .number = number};
}

/// No citation at all.
constexpr journal_citation Nothing() { return {}; }

TEST(JournalCitation, TheShapeIsTheWordAndANumberNearIt) {
  EXPECT_EQ(journal_citation_in("READ JOURNAL ENTRY 12"), Entry(12));
  EXPECT_EQ(journal_citation_in("JOURNAL 7"), Entry(7));
  EXPECT_EQ(journal_citation_in("SEE JOURNAL ENTRY 103 NOW"), Entry(103));
  EXPECT_EQ(journal_citation_in("A JOURNAL ENTRY 4 AND MORE TEXT"), Entry(4));
}

TEST(JournalCitation, EachSectionHasItsOwnWord) {
  // #218: three sections, each numbering from its own base, so the word
  // is what says which of them a number belongs to.
  EXPECT_EQ(journal_citation_in("TALE 4"), Tale(4));
  EXPECT_EQ(journal_citation_in("YOU HEAR TAVERN TALE 17"), Tale(17));
  EXPECT_EQ(journal_citation_in("READ PROCLAMATION 214"), Proclamation(214));
}

TEST(JournalCitation, TheSameNumberInTwoSectionsIsTwoCitations) {
  // The whole reason the answer is a pair and not a number.
  EXPECT_NE(journal_citation_in("JOURNAL ENTRY 4"),
            journal_citation_in("TALE 4"));
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 4").number,
            journal_citation_in("TALE 4").number);
}

TEST(JournalCitation, TheEarliestOneWinsWhicheverSectionItNames) {
  // Position outermost, word innermost — what it was when there was one
  // word, and what keeps a later sentence from outranking the drawn one.
  EXPECT_EQ(journal_citation_in("TALE 3 AND JOURNAL ENTRY 9"), Tale(3));
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 9 AND TALE 3"), Entry(9));
}

TEST(JournalCitation, WithoutTheWordItIsNotACitation) {
  EXPECT_EQ(journal_citation_in("ENTRY 12"), Nothing());
  EXPECT_EQ(journal_citation_in("YOU FIND 12 GOLD PIECES"), Nothing());
  EXPECT_EQ(journal_citation_in("JOURNAL"), Nothing());
  EXPECT_EQ(journal_citation_in("TALE"), Nothing());
  EXPECT_EQ(journal_citation_in("PROCLAMATION"), Nothing());
}

TEST(JournalCitation, ANumberTooFarAwayIsSomebodyElsesNumber) {
  // Twelve characters of reach: far enough for the word for an entry and
  // the punctuation round it, short enough that the next sentence's
  // numbers are not this citation's.
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 9"), Entry(9));
  EXPECT_EQ(journal_citation_in("JOURNAL AND THEN A LONG WAY OFF 9"),
            Nothing());
}

TEST(JournalCitation, ItIsAWholeWordAndNotAFragment) {
  EXPECT_EQ(journal_citation_in("JOURNALISM 4"), Nothing());
  EXPECT_EQ(journal_citation_in("ADJOURNAL 4"), Nothing());
  // The two words #218 added are short and ordinary, so this rule is
  // doing more work than it was: without it a stale tally and a talent
  // would both be citations.
  EXPECT_EQ(journal_citation_in("STALE 4"), Nothing());
  EXPECT_EQ(journal_citation_in("TALES 4"), Nothing());
  EXPECT_EQ(journal_citation_in("PROCLAMATIONS 4"), Nothing());
}

TEST(JournalCitation, ZeroAndARunTooLongAreNotEntryNumbers) {
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 0"), Nothing());
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 123456"), Nothing());
  EXPECT_EQ(journal_citation_in("JOURNAL ENTRY 9999"), Entry(9999));
}

TEST(JournalCitation, ASecondCitationInTheWindowStillCounts) {
  // The first occurrence has no number near it; the scan carries on
  // rather than answering nothing.
  EXPECT_EQ(journal_citation_in("JOURNAL AND ALSO THE JOURNAL ENTRY 5"),
            Entry(5));
}

TEST(JournalWindow, ACitationSplitAcrossTwoDrawsIsStillOne) {
  journal_state state;
  EXPECT_EQ(state.note_drawn_text("...read the Journal"), Nothing());
  EXPECT_EQ(state.note_drawn_text("Entry 21 before going on."), Entry(21));
  EXPECT_EQ(state.cited(), Entry(21));
}

TEST(JournalWindow, AMatchEmptiesTheWindowSoItCannotFireTwice) {
  journal_state state;
  EXPECT_EQ(state.note_drawn_text("Journal Entry 3"), Entry(3));
  EXPECT_EQ(state.note_drawn_text("and nothing more"), Nothing())
      << "the same characters must not match a second time";
}

TEST(JournalWindow, TwoStringsAreTwoWordsAndNeverOne) {
  journal_state state;
  EXPECT_EQ(state.note_drawn_text("JOURNA"), Nothing());
  EXPECT_EQ(state.note_drawn_text("L 4"), Nothing())
      << "two draws are two words; a citation cannot be spelled across the "
         "seam between them";
}

TEST(JournalWindow, ForgettingItLeavesNothingToMatchAgainst) {
  journal_state state;
  EXPECT_EQ(state.note_drawn_text("the journal"), Nothing());
  state.forget_citation();
  EXPECT_EQ(state.note_drawn_text("entry 6"), Nothing());
}

// ---------------------------------------------------------------------------
// The delivery buffer
// ---------------------------------------------------------------------------

TEST(JournalDelivery, AskingFirstMeansAnUnansweredCallIsNotTheLastAnswer) {
  journal_state state;
  state.ask(Entry(4));
  state.deliver("something");
  EXPECT_EQ(state.delivery(), journal_delivery::ready);
  state.ask(Entry(5));
  EXPECT_EQ(state.delivery(), journal_delivery::no_host);
  EXPECT_TRUE(state.text().empty());
}

TEST(JournalDelivery, NoTextIsItsOwnAnswerAndNotAReadyBlank) {
  journal_state state;
  state.ask(Entry(4));
  state.deliver("");
  EXPECT_EQ(state.delivery(), journal_delivery::no_text);
}

TEST(JournalDelivery, TextLongerThanTheBufferIsTruncatedAndSaysSo) {
  journal_state state;
  const std::string huge(journal_page_bytes + 100, 'x');
  state.ask(Entry(1));
  state.deliver(huge);
  EXPECT_EQ(state.delivery(), journal_delivery::ready);
  EXPECT_EQ(state.text().size(), journal_page_bytes);
  EXPECT_TRUE(state.truncated());
}

// ---------------------------------------------------------------------------
// The reader on the screen
// ---------------------------------------------------------------------------

TEST(JournalReader, ClosedItDrawsNothingAtAll) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.poll(4);
  EXPECT_FALSE(r.reader().reader_open());
  EXPECT_FALSE(r.reader().on_screen());
  for (unsigned x = 0; x < 4; ++x) {
    EXPECT_EQ(r.screen_pixel(automap_panel_x + x, automap_panel_y), 0);
  }
}

TEST(JournalReader, ACitationOpensTheEntryOnTheGamesOwnScreen) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 12;
  r.host.text = "A short line.";

  r.program_draws("Read journal entry 12.");
  EXPECT_EQ(r.host.calls, 1u);
  EXPECT_EQ(r.host.asked, journal_open_argument(Entry(12)));
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);

  r.adventuring();
  r.poll();
  EXPECT_TRUE(r.reader().on_screen());
  EXPECT_EQ(r.row_text(reader_title_y), centred("ENTRY 12"));
  EXPECT_EQ(r.row_text(reader_body_y), as_glyphs("A SHORT LINE."));
  EXPECT_EQ(r.row_text(reader_footer_y), centred("F1 CLOSES"))
      << "the footer names the key that does the next thing";

  // And it is on the planes, not only in the seam's own buffer.
  bool any = false;
  for (unsigned x = 0; x < automap_panel_width && !any; ++x) {
    for (unsigned y = 0; y < automap_panel_height && !any; ++y) {
      any = r.screen_pixel(automap_panel_x + x, automap_panel_y + y) != 0;
    }
  }
  EXPECT_TRUE(any) << "the page has to reach the EGA planes";
}

TEST(JournalReader, WithNoJournalACitationOpensNothing) {
  // #175: "reports that no journal is ingested, and the seam shows
  // nothing rather than a blank page".
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.empty = true;

  r.program_draws("Read journal entry 12.");
  EXPECT_EQ(r.host.calls, 1u) << "the host was asked; it had nothing";
  EXPECT_EQ(r.reader().delivery(), journal_delivery::no_journal);
  EXPECT_FALSE(r.reader().reader_open());

  r.adventuring();
  r.poll();
  EXPECT_FALSE(r.reader().on_screen());
}

TEST(JournalReader, WithNoHostAttachedItStillOpensNothing) {
  rig r;
  r.attach_video();
  r.enable();
  r.adventuring();
  r.program_draws("Read journal entry 12.");
  EXPECT_EQ(r.reader().delivery(), journal_delivery::no_host);
  EXPECT_FALSE(r.reader().reader_open());
}

TEST(JournalReader, TheKeyAsksForAnEntryAndReturnOpensIt) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 12;
  r.host.text = "Here is the entry.";

  r.type(key_f1);
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::asking);
  EXPECT_EQ(r.keys_waiting(), 0u) << "F1 is this seam's key and nobody else's";
  EXPECT_EQ(r.row_text(reader_title_y), centred("JOURNAL"));

  r.type(key_one);
  r.type(key_two);
  r.poll(2);
  EXPECT_EQ(r.reader().digits(), "12");
  EXPECT_EQ(r.row_text(reader_body_y + (4 * glyph_height)),
            prompt_row("ENTRY 12"));

  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::showing);
  EXPECT_EQ(r.host.asked, journal_open_argument(Entry(12)));
  EXPECT_EQ(r.row_text(reader_body_y), as_glyphs("HERE IS THE ENTRY."));
}

TEST(JournalReader, TheKeyPicksTheSectionAndTheAnswerIsThePair) {
  // #218: three numbered sections, so a player typing `4` at the prompt
  // has not yet said what they want. F1 is what says it — the key this
  // seam already owns, rather than one the automap might want.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds = Tale(4);
  r.host.text = "A tale.";

  r.type(key_f1);
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::asking);
  EXPECT_EQ(r.reader().asked_kind(), journal_kind::entry)
      << "the prompt opens on the section the game cites most";

  r.type(key_four);
  r.poll();
  EXPECT_EQ(r.row_text(reader_body_y + (4 * glyph_height)),
            prompt_row("ENTRY 4"));

  // Round the three and back to the start, with the panel saying which.
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.reader().asked_kind(), journal_kind::tale);
  EXPECT_EQ(r.row_text(reader_body_y + (4 * glyph_height)),
            prompt_row("TALE 4"))
      << "the digits are kept: picking a section is not retyping a number";

  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::showing);
  EXPECT_EQ(r.host.asked, journal_open_argument(Tale(4)))
      << "the host is asked for a tale, not for entry four";
  EXPECT_EQ(r.row_text(reader_title_y), centred("TALE 4"));
  EXPECT_EQ(r.row_text(reader_body_y), as_glyphs("A TALE."));
}

TEST(JournalReader, TheSectionGoesRoundAndComesBack) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();

  r.type(key_f1);
  r.poll();
  for (const journal_kind want :
       {journal_kind::tale, journal_kind::proclamation, journal_kind::entry}) {
    r.type(key_f1);
    r.poll();
    EXPECT_EQ(r.reader().asked_kind(), want);
  }
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::asking)
      << "F1 no longer closes the prompt; escape is what leaves it";
}

TEST(JournalReader, EscapeIsStillTheWayOutOfThePrompt) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();

  r.type(key_f1);
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::asking);
  r.type(key_escape);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed);
}

TEST(JournalReader, ACitedTaleIsNotTheEntryWithTheSameNumber) {
  // The whole of #218 in one case. The game cites tale twelve while entry
  // twelve is what the host holds; a build that carried the number alone
  // would put entry twelve on the screen and be sure it was right.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds = Entry(12);
  r.host.text = "The twelfth entry.";

  r.program_draws("read tale 12");
  r.poll();
  EXPECT_EQ(r.host.asked, journal_open_argument(Tale(12)));
  EXPECT_EQ(r.reader().delivery(), journal_delivery::no_entry);
}

TEST(JournalReader, TheSameNumberInAnotherSectionIsAFreshCitation) {
  // The reader leaves an entry alone when the game cites the one already
  // on the screen. That comparison is of the *pair*: entry twelve is up,
  // and tale twelve is a different text and has to be asked for.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds = Entry(12);
  r.host.text = "The twelfth entry.";

  r.program_draws("journal entry 12");
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);
  const unsigned after_first = r.host.calls;

  r.reader().forget_citation();
  r.program_draws("journal entry 12");
  r.poll();
  EXPECT_EQ(r.host.calls, after_first)
      << "the entry already on the screen is not asked for again";

  r.reader().forget_citation();
  r.program_draws("tale 12");
  r.poll();
  EXPECT_EQ(r.host.calls, after_first + 1U)
      << "the same number in another section is another text";
  EXPECT_EQ(r.host.asked, journal_open_argument(Tale(12)));
}

TEST(JournalReader, BackspaceRubsOutADigit) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();

  r.type(key_f1);
  r.poll();
  r.type(key_one);
  r.type(key_two);
  r.poll(2);
  ASSERT_EQ(r.reader().digits(), "12");
  r.type(key_backspace);
  r.poll();
  EXPECT_EQ(r.reader().digits(), "1");
}

TEST(JournalReader, AnEntryTheJournalHasNotSaysWhichKindOfNothingItIs) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 12;
  r.host.text = "Something.";

  r.type(key_f1);
  r.poll();
  r.type(key_two);
  r.poll();
  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.reader().delivery(), journal_delivery::no_entry);
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::showing)
      << "the player asked, so the answer is shown rather than swallowed";
  EXPECT_EQ(r.row_text(reader_body_y + (4 * glyph_height)),
            centred("NO SUCH ENTRY"));
}

TEST(JournalReader, ALongEntryIsWrappedAtTheWordAndPaged) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  // Two lines' worth, with a word that must not be split across them.
  r.host.text = "aaaa bbbb cccc dddd ee ffffffff gggg";

  r.program_draws("journal 1");
  r.adventuring();
  r.poll();
  EXPECT_EQ(r.row_text(reader_body_y), as_glyphs("AAAA BBBB CCCC DDDD EE"));
  EXPECT_EQ(r.row_text(reader_body_y + glyph_height),
            as_glyphs("FFFFFFFF GGGG"));
  EXPECT_EQ(r.reader().page_count(), 1u);
}

TEST(JournalReader, AWordLongerThanThePanelIsBrokenRatherThanDropped) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = std::string(30, 'q');

  r.program_draws("journal 1");
  r.adventuring();
  r.poll();
  EXPECT_EQ(r.row_text(reader_body_y), as_glyphs(std::string(22, 'q')));
  EXPECT_EQ(r.row_text(reader_body_y + glyph_height),
            as_glyphs(std::string(8, 'q')));
}

TEST(JournalReader, TheKeyTurnsThePagesAndThenPutsItAway) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  // Thirteen lines of one word each: one more than a page holds.
  std::string text;
  for (int line = 0; line < 13; ++line) {
    text += "wordwordwordwordwordwordword ";
  }
  r.host.text = text;

  r.program_draws("journal 1");
  r.adventuring();
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);
  ASSERT_GT(r.reader().page_count(), 1u);
  EXPECT_EQ(r.reader().page(), 0u);

  const unsigned pages = r.reader().page_count();
  for (unsigned page = 1; page < pages; ++page) {
    r.type(key_f1);
    r.poll();
    EXPECT_EQ(r.reader().page(), page);
  }
  // And on the last page it closes.
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed);
}

TEST(JournalReader, EscapeClosesItFromWhereverItIs) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.type(key_f1);
  r.poll();
  ASSERT_TRUE(r.reader().reader_open());
  r.type(key_escape);
  r.poll();
  EXPECT_FALSE(r.reader().reader_open());
  EXPECT_EQ(r.keys_waiting(), 0u);
}

TEST(JournalReader, WithNoFontInstalledNothingIsDrawn) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.no_font();
  r.host.holds.number = 1;
  r.host.text = "text";

  r.program_draws("journal 1");
  r.adventuring();
  r.no_font();
  r.poll();
  EXPECT_FALSE(r.reader().on_screen())
      << "a page rasterized out of an empty buffer is a black rectangle";
}

TEST(JournalReader, OffAScreenWithARosterItDrawsNothing) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = "text";
  r.program_draws("journal 1");
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);

  r.adventuring();
  r.put_byte(rig::dgroup(), data_game_mode, mode_title);
  r.poll();
  EXPECT_FALSE(r.reader().on_screen());
}

TEST(JournalReader, SomethingClearingTheseCellsTakesThePageWithIt) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = "text";
  r.program_draws("journal 1");
  r.adventuring();
  r.poll();
  ASSERT_TRUE(r.reader().on_screen());

  // The whole screen going certainly includes these cells.
  r.stand_on(r.point(4));
  r.pc().step();
  EXPECT_TRUE(r.reader().covered());
  EXPECT_FALSE(r.reader().on_screen());

  r.adventuring();
  r.poll();
  EXPECT_FALSE(r.reader().on_screen()) << "covered, so nothing is drawn";

  // And when the roster comes back the page does too.
  r.stand_on(r.point(5));
  r.pc().step();
  EXPECT_FALSE(r.reader().covered());
  r.adventuring();
  r.poll();
  EXPECT_TRUE(r.reader().on_screen());
}

// ---------------------------------------------------------------------------
// The keys the program keeps
// ---------------------------------------------------------------------------

TEST(JournalKeys, OffAScreenWithARosterEvenTheKeyIsNobodys) {
  // A key claimed where nothing can be drawn is a key the player pressed
  // and saw no answer to.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_byte(rig::dgroup(), data_game_mode, mode_title);
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_FALSE(r.reader().reader_open());
}

TEST(JournalReader, AnEntryOpenedOffARosterScreenComesUpWhenOneReturns) {
  // A citation is watched wherever the program draws it; only the
  // presentation waits for a screen this panel can be on.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = "text";

  r.adventuring();
  r.put_byte(rig::dgroup(), data_game_mode, mode_title);
  r.program_draws("journal 1");
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);
  r.poll();
  EXPECT_FALSE(r.reader().on_screen());

  r.adventuring();
  r.poll();
  EXPECT_TRUE(r.reader().on_screen());
}

TEST(JournalKeys, WithTheReaderDownEverythingButTheKeyIsTheProgramsOwn) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  for (const std::uint16_t key :
       {key_escape, key_return, key_backspace, key_tab, key_one}) {
    r.type(key);
  }
  r.poll(6);
  EXPECT_EQ(r.keys_waiting(), 5u)
      << "not one of these is this seam's while the reader is closed";
}

TEST(JournalKeys, WhileAPageIsUpReturnStaysTheProgramsOwn) {
  // A citation opens the reader in the middle of a story event, and the
  // key that turns the game's own page has to stay the game's.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = "text";
  r.program_draws("journal 1");
  r.adventuring();
  r.poll();
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::showing);

  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 1u);
}

TEST(JournalKeys, AKeyBehindAnotherIsClaimedOnThePassItWouldHaveBeenRead) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.type(key_two);
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 2u) << "only the head of the ring is ever taken";
  EXPECT_FALSE(r.reader().reader_open());
}

TEST(JournalKeys, NothingIsTakenWhileTheProgramsPushbackSlotIsArmed) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_byte(rig::dgroup(), data_key_pushback, 0x50);
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_FALSE(r.reader().reader_open());
}

TEST(JournalKeys, TheBlockingReadClaimsToo) {
  // The program is about to wait for a key, and a poll may never have
  // preceded it. Point 1 is the same claim as point 0.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.type(key_f1);
  r.stand_on(r.point(1));
  r.pc().step();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::asking);
}

TEST(JournalKeys, TheBlockingReadIsAnsweredWithSomethingIgnorable) {
  // The one thing the poll may not do and this point must: put a key back.
  // The program at a blocking read has committed to being handed one, and
  // a read the seam empties goes to sleep inside the BIOS - where no point
  // of this engine is reached, and the *next* key the player types is
  // handed to the program unseen. So exactly one keystroke comes back, and
  // it is the one the program's own menu-bar routine throws away.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.type(key_f1);
  r.stand_on(r.point(1));
  r.pc().step();

  ASSERT_EQ(r.keys_waiting(), 1u) << "the read is answered, not emptied";
  const std::uint16_t head = r.word_at(bda::segment, bda::keyboard_buffer_head);
  EXPECT_EQ(r.word_at(bda::segment, head), key_ignored);
}

TEST(JournalKeys, ThePollLeavesNothingBehind) {
  // The counterpart, and the reason the answer belongs at the read alone:
  // at the poll the program has asked a question it can be told "no" to,
  // so a claimed key leaves an empty buffer and the program goes round its
  // own loop none the wiser.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.type(key_f1);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 0u);
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::asking);
}

// ---------------------------------------------------------------------------
// Fidelity
// ---------------------------------------------------------------------------

TEST(JournalFidelity, OnAndNothingCitedLeavesTheRunIdentical) {
  const auto run = [](bool on) {
    rig r;
    if (on) {
      EXPECT_EQ(r.pc().seams().enable("journal"), seam_reason::none);
      EXPECT_TRUE(r.pc().seams().armed());
    }
    r.adventuring();

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

TEST(JournalFidelity, WatchingTheProgramsTextIsNotMachineState) {
  rig r;
  r.attach_host();
  r.enable();
  r.adventuring();
  r.program_draws("nothing to see here at all");
  ASSERT_EQ(r.reader().cited(), Nothing());

  rig plain;
  plain.adventuring();
  plain.program_draws("nothing to see here at all");
  EXPECT_EQ(hash_state(r.pc()), hash_state(plain.pc()));
}

TEST(JournalFidelity, AResetMachineHasReadNothing) {
  rig r;
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds.number = 1;
  r.host.text = "text";
  r.program_draws("journal 1");
  ASSERT_TRUE(r.reader().reader_open());
  r.pc().reset();
  EXPECT_FALSE(r.reader().reader_open());
  EXPECT_EQ(r.reader().cited(), Nothing());
  EXPECT_EQ(r.reader().delivery(), journal_delivery::none);
}

TEST(JournalDefinition, ItIsACommandAndNotAPullAndItNamesTheBaseline) {
  const rig r;
  const seam_definition* journal = r.pc().seams().find("journal");
  ASSERT_NE(journal, nullptr);
  EXPECT_FALSE(journal->trigger) << "a key is not a pull";
  // Ungated on purpose: the document table has no journal row, so a gate
  // would leave this seam inert for every player alive (seam_journal.cpp).
  EXPECT_EQ(journal->gate, document_kind::none);
  EXPECT_EQ(journal->schema, seam_schema_version);
  EXPECT_EQ(journal->points.size(), 10u);
  std::size_t resident = 0;
  std::size_t overlaid = 0;
  for (const seam_point& point : journal->points) {
    EXPECT_FALSE(point.at_every_step) << "every point of this seam has an "
                                         "address, which is where its safety "
                                         "comes from";
    if (point.module.file == resident_image.file) {
      ++resident;
    } else {
      ++overlaid;
    }
  }
  // Six in the resident image, and four in the adventuring loop's own
  // module (#221) — a splice-in and a splice-out per view mode. The pairs
  // have to be in the overlay: the promise is that the program's own bar
  // string is unchanged outside the one call that drew it, and only a
  // point at the call's own return can keep it.
  EXPECT_EQ(resident, 6u);
  EXPECT_EQ(overlaid, 4u);
  ASSERT_EQ(journal->fingerprints.size(), 1u);
  EXPECT_EQ(journal->fingerprints.front(),
            known_editions().front().fingerprint);
}

// ---------------------------------------------------------------------------
// The command on the party's own bar (M5-E4a, #221)
// ---------------------------------------------------------------------------

/// The two bars and their points, as the seam's facts have them. Restated
/// here rather than shared, so a change to either has to be made twice on
/// purpose (`docs/seams.md` §8.1).
constexpr std::uint16_t bar_area = 0x04B6;
constexpr std::uint16_t bar_view = 0x04DF;
constexpr std::uint16_t area_before = 0x09D0;
constexpr std::uint16_t area_after = 0x09D5;
constexpr std::uint16_t view_before = 0x0C40;
constexpr std::uint16_t view_after = 0x0C45;

/// Stand-ins for the program's own two bars: the same shape and the same
/// lengths, in this file's own words. Nothing of the program's text is
/// here, which is the whole reason the splice appends rather than
/// composes — it does not care what the bar says.
constexpr std::string_view area_words = "Aaaa Bbbb Cccc Dddddd Eeeeee Ffff";
constexpr std::string_view view_words = "Bbbb Cccc Dddddd Eeeeee Ffff";

TEST(JournalNotes, TheCommandGoesOnTheBarAndComesOffAgain) {
  rig r;
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);

  r.stand_in_adventure(area_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), std::string(area_words) + " Notes")
      << "the program draws the bar it is handed, so the command has to be"
         " in the string before the call";

  r.stand_in_adventure(area_after, 'X');
  r.put_byte(r.dgroup(), 0x0600 - 0x04, 0);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), area_words)
      << "outside the one call that drew it, the program's string is the"
         " program's string";
}

TEST(JournalNotes, BothViewModesHaveIt) {
  rig r;
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);
  r.put_bar(bar_view, view_words);

  r.stand_in_adventure(area_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), std::string(area_words) + " Notes");
  EXPECT_EQ(r.bar_at(bar_view), view_words) << "one point, one bar";

  r.stand_in_adventure(view_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_view), std::string(view_words) + " Notes");

  // And each mode takes its own back off at its own return.
  r.stand_in_adventure(view_after, 0);
  r.put_byte(r.dgroup(), 0x0600 - 0x04, 1);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_view), view_words);
  EXPECT_EQ(r.bar_at(bar_area), std::string(area_words) + " Notes")
      << "one point, one bar, on the way out as well as the way in";
}

TEST(JournalNotes, SplicingTwiceDoesNotDoubleIt) {
  rig r;
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);

  r.stand_in_adventure(area_before);
  r.box->step();
  r.stand_in_adventure(area_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), std::string(area_words) + " Notes");
}

TEST(JournalNotes, ABarWithNoRoomIsLeftAlone) {
  // The slot is a Pascal string[40]; the splice refuses rather than
  // overruns, and the player sees the game's own bar.
  rig r;
  r.attach_host();
  r.enable();
  const std::string full(36, 'A');
  r.put_bar(bar_area, full);

  r.stand_in_adventure(area_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), full) << "36 + 6 is over the forty it has";
}

TEST(JournalNotes, ChoosingItOpensTheReader) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::closed);

  r.one_bar_pass(area_before, area_after, 'N');
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::listing)
      << "the command opens the journal's own screen, not the prompt";
  EXPECT_EQ(r.reader().asked_kind(), journal_kind::entry);
  EXPECT_TRUE(r.reader().digits().empty());
}

TEST(JournalNotes, AnotherLetterIsNoneOfThisSeamsBusiness) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);

  for (const std::uint8_t letter : {'A', 'C', 'V', 'E', 'S', 'L'}) {
    r.one_bar_pass(area_before, area_after, letter);
    EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed)
        << "letter " << static_cast<char>(letter);
  }
}

TEST(JournalNotes, AKeyTheRoutineHandledItselfIsNotACommand) {
  // The loop's out-parameter is non-zero for a movement key the routine
  // translated, and the letter in AL is then not a command off the bar at
  // all. Reading it as one would open the reader on a keypad press.
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.put_bar(bar_area, area_words);

  r.one_bar_pass(area_before, area_after, 'N', 1);
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed);
}

TEST(JournalNotes, ItIsNotOfferedTwiceWhileTheReaderIsUp) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_bar(bar_area, area_words);
  r.reader().note_seen(Entry(3), 8, 29, 20, 15);
  r.reader().note_seen(Tale(12), 8, 29, 21, 44);

  r.one_bar_pass(area_before, area_after, 'N');
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);
  r.reader().move_list_cursor(1);
  ASSERT_EQ(r.reader().list_cursor(), 1u);

  // The player can see the journal and the way out of it is the way out
  // of it; picking the command again must not put them back at the top.
  r.one_bar_pass(area_before, area_after, 'N');
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::listing);
  EXPECT_EQ(r.reader().list_cursor(), 1u);
}

TEST(JournalNotes, WithTheSeamOffTheBarIsNeverTouched) {
  // The fidelity invariant, on the one thing this addition writes.
  rig r;
  r.attach_host();
  r.put_bar(bar_area, area_words);

  r.stand_in_adventure(area_before);
  r.box->step();
  EXPECT_EQ(r.bar_at(bar_area), area_words);
}

// ---------------------------------------------------------------------------
// The log of what the game has said (M5-E4b, #222)
// ---------------------------------------------------------------------------

TEST(JournalLog, ItStartsEmptyAndFillsNewestFirst) {
  journal_state state;
  EXPECT_TRUE(state.seen().empty())
      << "a player who has been told nothing has read nothing";
  EXPECT_FALSE(state.seen_changed());

  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.note_seen(Tale(12), 8, 29, 21, 44);
  state.note_seen(Proclamation(109), 8, 29, 22, 19);

  ASSERT_EQ(state.seen().size(), 3u);
  EXPECT_EQ(state.seen()[0].what, Proclamation(109));
  EXPECT_EQ(state.seen()[1].what, Tale(12));
  EXPECT_EQ(state.seen()[2].what, Entry(3));
  EXPECT_EQ(state.seen()[0].hour, 22);
  EXPECT_EQ(state.seen()[0].minute, 19);
  EXPECT_TRUE(state.seen_changed());
}

TEST(JournalLog, CitingSomethingTwiceMovesItUpRatherThanRepeatingIt) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.note_seen(Tale(12), 8, 29, 21, 44);
  ASSERT_TRUE(state.mark_seen_read(Entry(3)));

  state.note_seen(Entry(3), 8, 30, 9, 5);
  ASSERT_EQ(state.seen().size(), 2u) << "one line, not two";
  EXPECT_EQ(state.seen()[0].what, Entry(3));
  EXPECT_EQ(state.seen()[0].day, 30) << "re-dated to when it was said again";
  EXPECT_TRUE(state.seen()[0].read)
      << "the game repeating itself does not unread what the player read";
}

TEST(JournalLog, ReadingIsTheOnlyThingThatTakesTheStarOff) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  EXPECT_FALSE(state.seen()[0].read);
  EXPECT_TRUE(state.mark_seen_read(Entry(3)));
  EXPECT_TRUE(state.seen()[0].read);

  // Something nothing cited has no line to mark, which is the ordinary
  // answer for an entry a player asked for at the prompt.
  EXPECT_FALSE(state.mark_seen_read(Entry(58)));
  EXPECT_FALSE(state.mark_seen_read(Tale(3)))
      << "the same number in another section is another thing";
}

TEST(JournalLog, TheOldestFallsOffTheEnd) {
  journal_state state;
  for (unsigned i = 1; i <= journal_log_rows + 5; ++i) {
    state.note_seen(Entry(static_cast<std::uint16_t>(i)), 8, 30, 10, 0);
  }
  ASSERT_EQ(state.seen().size(), journal_log_rows);
  EXPECT_EQ(state.seen().front().what,
            Entry(static_cast<std::uint16_t>(journal_log_rows + 5)));
  EXPECT_EQ(state.seen().back().what, Entry(6))
      << "the first five are gone, oldest first";
}

TEST(JournalLog, TheFlagIsWhatStopsAHostWritingOnEveryCitation) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  ASSERT_TRUE(state.seen_changed());
  state.set_seen_changed(false);

  EXPECT_TRUE(state.mark_seen_read(Entry(3)));
  EXPECT_TRUE(state.seen_changed()) << "reading it is a change";
  state.set_seen_changed(false);

  EXPECT_TRUE(state.mark_seen_read(Entry(3)));
  EXPECT_FALSE(state.seen_changed())
      << "reading what was already read changes nothing";
}

TEST(JournalLog, AResetMachineHasBeenToldNothing) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.clear();
  EXPECT_TRUE(state.seen().empty());
  EXPECT_FALSE(state.seen_changed());
}

TEST(JournalLog, ACitationGoesIntoItWithTheMachinesOwnClock) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.host.holds = Entry(12);
  r.host.text = "The twelfth entry.";
  ASSERT_TRUE(r.pc().set_wall_time(wall_time{
      .year = 2026, .month = 8, .day = 30, .hour = 22, .minute = 19}));

  r.program_draws("journal entry 12");
  r.poll();

  ASSERT_EQ(r.reader().seen().size(), 1u);
  const journal_seen_row& row = r.reader().seen().front();
  EXPECT_EQ(row.what, Entry(12));
  EXPECT_EQ(row.month, 8);
  EXPECT_EQ(row.day, 30);
  EXPECT_EQ(row.hour, 22);
  EXPECT_EQ(row.minute, 19);
  EXPECT_TRUE(row.read) << "the citation opened it, so it has been read";
}

TEST(JournalLog, TheLogIsObservationAndNotMachineState) {
  // The same three terms the automap's exploration is held to. Told
  // directly rather than through a citation, because a citation also
  // *opens* the reader and what that draws is a real difference on a real
  // screen — the enhancement, not a leak. What is claimed here is
  // narrower and is the thing that matters: the log itself is not state.
  rig r;
  const state_hashes before = hash_state(r.pc());

  r.reader().note_seen(Entry(12), 8, 30, 22, 19);
  r.reader().note_seen(Tale(4), 8, 30, 22, 20);
  ASSERT_EQ(r.reader().seen().size(), 2u);

  EXPECT_EQ(before, hash_state(r.pc()));
}

// ---------------------------------------------------------------------------
// The journal's own screen (M5-E4b, #222)
// ---------------------------------------------------------------------------

TEST(JournalList, TheCursorStepsAndStopsAtTheEnds) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.note_seen(Tale(12), 8, 29, 21, 44);
  state.note_seen(Proclamation(109), 8, 29, 22, 19);
  EXPECT_EQ(state.list_cursor(), 0u) << "it opens on the newest";

  state.move_list_cursor(1);
  EXPECT_EQ(state.list_cursor(), 1u);
  state.move_list_cursor(-1);
  EXPECT_EQ(state.list_cursor(), 0u);

  // Stops rather than wraps, at both ends: a cursor that jumped from one
  // to the other would lose a player holding a key down.
  state.move_list_cursor(-1);
  EXPECT_EQ(state.list_cursor(), 0u);
  state.move_list_cursor(9);
  EXPECT_EQ(state.list_cursor(), 2u);
  state.move_list_cursor(9);
  EXPECT_EQ(state.list_cursor(), 2u);
}

TEST(JournalList, ACursorPastTheEndOfAShrunkenLogIsStillOnIt) {
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.note_seen(Tale(12), 8, 29, 21, 44);
  state.move_list_cursor(1);
  ASSERT_EQ(state.list_cursor(), 1u);

  state.clear_seen();
  EXPECT_EQ(state.list_cursor(), 0u) << "an empty log has nowhere to point";
}

TEST(JournalList, AMovementKeyWithTheJournalDownIsThePartys) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();

  r.type(key_step_down);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 1u)
      << "with nothing up, the keys that move the party are the program's";
}

TEST(JournalList, AMovementKeyWithTheListUpStepsTheList) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_bar(bar_area, area_words);
  r.reader().note_seen(Entry(3), 8, 29, 20, 15);
  r.reader().note_seen(Tale(12), 8, 29, 21, 44);

  r.one_bar_pass(area_before, area_after, 'N');
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);

  r.type(key_step_down);
  r.poll();
  EXPECT_EQ(r.keys_waiting(), 0u)
      << "and this seam's while the list is the thing on the screen";
  EXPECT_EQ(r.reader().list_cursor(), 1u);
}

TEST(JournalList, ReturnOpensTheLineItIsPointingAt) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_bar(bar_area, area_words);
  r.host.holds = Tale(12);
  r.host.text = "A tale.";
  r.reader().note_seen(Entry(3), 8, 29, 20, 15);
  r.reader().note_seen(Tale(12), 8, 29, 21, 44);

  r.one_bar_pass(area_before, area_after, 'N');
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);
  ASSERT_EQ(r.reader().seen().front().what, Tale(12));

  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::showing);
  EXPECT_EQ(r.host.asked, journal_open_argument(Tale(12)))
      << "the line the cursor was on, not the number the prompt held";
  EXPECT_TRUE(r.reader().seen().front().read)
      << "opening it is what takes the star off";
}

TEST(JournalList, ReturnOnAnEmptyListOpensNothing) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_bar(bar_area, area_words);

  r.one_bar_pass(area_before, area_after, 'N');
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);
  r.type(key_return);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::listing);
  EXPECT_EQ(r.host.calls, 0u);
}

TEST(JournalList, EscapeLeavesIt) {
  rig r;
  r.attach_video();
  r.attach_host();
  r.enable();
  r.adventuring();
  r.put_bar(bar_area, area_words);

  r.one_bar_pass(area_before, area_after, 'N');
  ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);
  r.type(key_escape);
  r.poll();
  EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed);
}

TEST(JournalList, TheLetterOnItsOwnBottomRowLeavesIt) {
  // The screen says `EXIT`, and the letter of a word on a bar is how this
  // game leaves every screen it has. Without this the only way out was a
  // key nothing on the screen named, and `E` reached the program's own
  // bar underneath and encamped instead.
  for (const std::uint16_t key :
       {std::uint16_t{0x1245}, std::uint16_t{0x1265}}) {
    rig r;
    r.attach_video();
    r.attach_host();
    r.enable();
    r.adventuring();
    r.put_bar(bar_area, area_words);

    r.one_bar_pass(area_before, area_after, 'N');
    ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);
    r.type(key);
    r.poll();
    EXPECT_EQ(r.reader().reader(), journal_reader_mode::closed)
        << "either case of the letter the screen names";
  }
}

TEST(JournalList, NoKeyAtAllReachesTheProgramWhileItIsUp) {
  // The list covers the program's own screen and the program's own command
  // bar goes on being live underneath it, so a key this seam left alone
  // chose a command, or walked the party, where nobody could see it - and
  // the program then drew its bar and its status line back over the
  // journal to prove it. Nothing gets past.
  // `S`earch, `C`ast, `V`iew, the left arrow that turns the party, and the
  // space the bar routine ends on - a fresh machine each, because what one
  // key does to a screen is not what this is asking.
  for (const std::uint16_t key :
       {std::uint16_t{0x1F73}, std::uint16_t{0x2E43}, std::uint16_t{0x2F56},
        std::uint16_t{0x4B00}, std::uint16_t{0x3920}}) {
    rig r;
    r.attach_video();
    r.attach_host();
    r.enable();
    r.adventuring();
    r.put_bar(bar_area, area_words);

    r.one_bar_pass(area_before, area_after, 'N');
    ASSERT_EQ(r.reader().reader(), journal_reader_mode::listing);

    r.type(key);
    r.poll();
    EXPECT_EQ(r.keys_waiting(), 0u) << "key " << key;
    EXPECT_EQ(r.reader().reader(), journal_reader_mode::listing)
        << "and none of them is a way out either: key " << key;
  }
}

TEST(JournalList, ThePaintIsStartedAgainWheneverWhatItShowsChanges) {
  // It is drawn over several arrivals, so "how far have I got" has to go
  // back to nothing when the thing being drawn is no longer the same.
  journal_state state;
  state.note_seen(Entry(3), 8, 29, 20, 15);
  state.note_seen(Tale(12), 8, 29, 21, 44);
  state.set_list_drawn(5);
  state.move_list_cursor(1);
  EXPECT_EQ(state.list_drawn(), 0u);
}

// ---------------------------------------------------------------------------
// What the panel can draw (M5-E4c, #219)
// ---------------------------------------------------------------------------
//
// Every non-ASCII character below is written as its own bytes rather than
// as a character in this file, because what is being tested is what
// happens to *bytes* - and a test whose meaning depended on this file's
// encoding would be testing the wrong thing.

/// `journal_drawable` over a buffer big enough for anything these cases
/// hand it, as a string.
[[nodiscard]] std::string Drawable(std::string_view text) {
  std::array<char, 256> into{};
  const journal_drawn out = journal_drawable(text, into);
  EXPECT_TRUE(out.complete);
  return {into.data(), out.written};
}

TEST(JournalDrawable, PlainTextIsItself) {
  EXPECT_EQ(Drawable("A ROUGHLY DRAWN CLOTH MAP."),
            "A ROUGHLY DRAWN CLOTH MAP.");
  EXPECT_EQ(Drawable("two\nlines"), "two\nlines")
      << "the layout above reads newlines, so they survive";
}

TEST(JournalDrawable, TheQuotationMarksAnEngineActuallyProduces) {
  // Two hundred and twenty-two of the two hundred and twenty-nine
  // non-ASCII characters in a real ingestion are these four.
  EXPECT_EQ(Drawable("\xE2\x80\x98Tale 4"), "'Tale 4");
  EXPECT_EQ(Drawable("\xE2\x80\x99"), "'");
  EXPECT_EQ(Drawable("\xE2\x80\x9CI am Yarash!\xE2\x80\x9D"),
            "\"I am Yarash!\"");
}

TEST(JournalDrawable, DashesAndAnEllipsis) {
  EXPECT_EQ(Drawable("a \xE2\x80\x94 b"), "a - b");
  EXPECT_EQ(Drawable("a \xE2\x80\x93 b"), "a - b");
  EXPECT_EQ(Drawable("and so\xE2\x80\xA6"), "and so...")
      << "three stops, because that is what it reads as";
}

TEST(JournalDrawable, OneGlyphOutOfOneCodePoint) {
  // The whole bug: three bytes of UTF-8 drew as three glyphs, so an entry
  // opening with a curly quote opened with three pieces of furniture.
  EXPECT_EQ(Drawable("\xE2\x80\x98").size(), 1u);
  EXPECT_EQ(Drawable("\xE2\x80\x9C").size(), 1u);
  EXPECT_EQ(Drawable("\xC2\xAE").size(), 1u);
}

TEST(JournalDrawable, WhatHasNoGlyphLooksLikeIt) {
  // The other seven characters a real ingestion produced are misreads of
  // something, and a reader is better told than shown nothing.
  EXPECT_EQ(Drawable("\xC2\xAE"), "?");
  EXPECT_EQ(Drawable("\xE2\x84\xA2"), "?");
  EXPECT_EQ(Drawable("\xE4\xB8\x80"), "?");
  EXPECT_EQ(Drawable("a\xC2\xA2"
                     "b"),
            "a?b")
      << "and never silently dropped";
}

TEST(JournalDrawable, ControlCharactersBecomeSpaces) {
  EXPECT_EQ(Drawable("a\tb"), "a b");
  EXPECT_EQ(Drawable("a\rb"), "a b");
}

TEST(JournalDrawable, IllFormedBytesAreSubstitutedAndAlwaysAdvance) {
  // A store is a file a person may edit, so it may not be valid UTF-8 at
  // all. Every one of these consumes exactly one byte, so no input can
  // make the loop stand still.
  EXPECT_EQ(Drawable("a\x80"
                     "b"),
            "a?b")
      << "a stray continuation byte";
  EXPECT_EQ(Drawable("a\xC3"), "a?") << "a sequence that runs off the end";
  EXPECT_EQ(Drawable("a\xE2\x28\xA1"
                     "b"),
            "a?(?b")
      << "a continuation byte that is not one";
  EXPECT_EQ(Drawable("\xFF\xFE"), "??");
}

TEST(JournalDrawable, ItStopsOnAFullBufferRatherThanHalfACharacter) {
  std::array<char, 4> into{};
  const journal_drawn out = journal_drawable("ab\xE2\x80\xA6", into);
  EXPECT_EQ(std::string(into.data(), out.written), "ab")
      << "an ellipsis is three and two were left, so it did not go in";
  EXPECT_FALSE(out.complete);
}

TEST(JournalDelivery, APageArrivesAsSomethingThePanelCanDraw) {
  journal_state state;
  state.ask(Entry(4));
  state.deliver(
      "\xE2\x80\x98"
      "A roughly drawn cloth map.\xE2\x80\x99");
  EXPECT_EQ(state.delivery(), journal_delivery::ready);
  EXPECT_EQ(state.text(), "'A roughly drawn cloth map.'");
  EXPECT_FALSE(state.truncated());
}

TEST(JournalDelivery, ALongPageIsNoLongerCutThroughACharacter) {
  journal_state state;
  // Fill the buffer to one byte short, then hand it something that draws
  // as three: it has to be left out whole rather than half written.
  //
  // An ellipsis rather than a dash, because a dash *is* one byte once it
  // has been made drawable - which is the point of doing this here, and
  // was the first version of this test being wrong about its own subject.
  std::string page(journal_page_bytes - 1, 'x');
  page += "\xE2\x80\xA6";
  state.ask(Entry(4));
  state.deliver(page);
  EXPECT_EQ(state.text().size(), journal_page_bytes - 1);
  EXPECT_TRUE(state.truncated());
  for (const char ch : state.text()) {
    ASSERT_EQ(ch, 'x');
  }
}

}  // namespace
}  // namespace amberfolio::machine
