// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal reader: PLAN.md §5 item 2's in-game half, M5-E4 (#175).
//
// A page of the player's own Adventurer's Journal, on the game's own
// screen, in the game's own glyphs — opened by the game when it cites an
// entry, and by the player when they want one it has not.
//
// #174 built everything under this: a player's document located entry by
// entry, inflated, read once by an OCR engine and kept as text on their
// own machine (`docs/journal.md`). This is the one consumer that store
// was written for, and the entire distance between the two is one host
// service and one buffer (`journal.h`).
//
//
// The three decisions (docs/seams.md §8)
// --------------------------------------
//
// **Its surface is a command**, and half of it is not a key at all. When
// the game says to read an entry, the entry opens: that is the whole
// enhancement, and it is why the citation watch is a point rather than a
// convenience. The other half is a key, for an entry the game has not
// cited — and a key rather than a spliced menu command because the
// adventuring screen's bar has neither the room for another word nor a
// point in this tree at the routine that hands its answer back
// (`seam_encamp_fix.cpp` has both for the camp bar; neither is a fact
// about this one).
//
// **Its points are addresses** — six, all in the resident image, and
// **not one of them is new to this tree**. Five are the automap's, for
// the same five reasons, and the sixth is the string drawer the Encamp
// Fix already calls (`image_draw_string`, `0x076B6`), watched at its
// entry instead. That is worth saying out loud: an enhancement that adds
// no address is an enhancement that cannot be wrong about one.
//
// **What it refuses**: it declines when the data segment is not where the
// fact table says, when the string a draw point was handed is not a
// Pascal string in memory it may read, and when the program has not
// installed its font — a reader with no glyphs is a black rectangle, and
// black already means something on this panel. It draws nothing at all
// unless the program is on a screen that has a party roster, and it opens
// nothing on a citation when the host has no text for it: **"nothing" is
// the answer to "you have not ingested a journal", not a blank page**
// (#175).
//
//
// Where it goes, and why there
// ----------------------------
//
// The same rect as the automap's panel, from `automap.h`, which derives
// it once: the interior of the adventuring screen's right-hand frame less
// the program's own status row — 176 by 112 pixels, twenty-two columns of
// the program's eight-pixel font by fourteen rows.
//
// It is there because it is **the one region of this program's screen a
// seam can take and give back**. The panel's cells are the party roster's,
// and the program can be asked to paint the roster again from live state
// (`give_the_roster_back()`); nothing else on the adventuring screen has
// that property. A wider reader — over the viewport, or the whole screen —
// would have nothing to restore what it covered, and the M5-E2d bug is
// exactly what that costs: closing a panel through the program's screen
// composer painted the 3D view over the vendor the player was talking to.
// Twelve rows of twenty-two characters is what that constraint pays for,
// and paging is what makes it enough.
//
// **The two panels are the same pixels**, so the reader is modal over the
// map: while it is open the automap does not draw (one condition in
// `seam_automap.cpp`), and the map comes back on its own when the entry
// is put away. Neither seam knows anything else about the other, and
// either works with the other switched off.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// Addresses and a format description, which is the direction
// CONTRIBUTING.md allows. Not a byte of the program is reproduced here,
// and — this is the part that matters for a *reader* — not a word of the
// program's text either. The citation watch matches a shape, not a
// sentence: the word this enhancement is named after and a number within
// reach of it (`journal_citation_in()`, journal.h).
//
// **Every string the program draws goes through one routine.** It takes a
// column, a row, a colour and a far pointer to a Pascal string, and cleans
// ten bytes; the Encamp Fix calls it to write its report and this watches
// it. At its entry the stack is the caller's frame with the far return
// address on top, so the arguments are at fixed offsets from SP.
//
// **A citation may arrive in two pieces.** The routine draws one string at
// one cell, so a sentence wrapped across two lines of a message panel is
// two calls. The watch therefore keeps a short rolling window of what has
// been drawn and matches against that (journal.h), which costs ninety-six
// bytes and removes a whole class of "it works on one screen and not the
// next".
//
// Everything else — the two keyboard entries, the two clears, the roster
// drawer and its return — is `seam_automap.cpp`'s fact table, restated
// here because a seam states its own facts (`seam_cheats_test.cpp`'s rule,
// applied to a seam rather than to a test).
//
//
// The keys, and the one that is nobody else's
// -------------------------------------------
//
// **F1 opens the reader, turns its pages, and closes it on the last one.**
// It is claimed the way the automap claims Tab — taken out of the BIOS
// buffer at 40:1Eh before the program's own key routine looks, so the
// program observes exactly what it would have observed had the key never
// been typed — and it is safe on a stronger argument than Tab's. A
// function key has **no character at all**: `keyboard.h`'s table answers
// AL=0 for the whole F1-F10 row. This program selects commands off its
// bars by character, so a key with none cannot be a command on any of
// them; and the extended keystrokes it does act on at their scan code are
// the numeric keypad's, which F1 is not one of. F11 and F12 are the SDL
// host's own keys and never reach the machine (`docs/hosts.md` §3), so
// F1 is the first key of that row that does.
//
// The rest are claimed **only while the reader is the thing on the
// screen**, which is the modal claim the automap's roster-cursor keys
// already make: Escape closes, Backspace goes back a page or rubs out a
// digit, and while the prompt is up the digits and Return are its own.
// Space and Return are deliberately *not* taken while a page is up — a
// citation opens the reader in the middle of a story event, and the key
// that turns the game's own page has to stay the game's.
//
//
// The fidelity claim, stated for this seam (docs/seams.md §8.5)
// -------------------------------------------------------------
//
//   **On, with no citation drawn and F1 never pressed, a run is byte for
//   byte the run with the seam off.** Every point reads and none of them
//   writes: no keystroke is claimed because none is there to claim, no
//   port is written, no pixel is drawn, and everything the seam learns
//   goes into `machine::journal()`, which is not machine state
//   (`journal.h`).
//
// The second sentence is narrower than the automap's and says so. A
// citation opens the reader with nobody having asked, which is the
// enhancement: from the moment one is drawn the run is a run with a panel
// on its screen. What still holds is that the *program's* input is
// untouched until the player presses a key at it — the reader draws, and
// takes nothing, until F1.
//
// Both are tests (`tests/core/machine/seam_journal_test.cpp`).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts
// ---------------------------------------------------------------------------

/// The SHA-256 of the program image every offset below is a fact about —
/// the baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> journal_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// **The points**, as offsets from the image segment, all in the resident
/// image. Five of the six are `seam_automap.cpp`'s and the sixth is
/// `seam_encamp_fix.cpp`'s call target watched at its entry, so every one
/// of them has been reached on a driven run of the real program before
/// this file existed.
constexpr std::uint32_t key_pending_entry = 0xA6FD;
constexpr std::uint32_t key_read_entry = 0xA70F;
constexpr std::uint32_t clear_region_entry = 0x4047;
constexpr std::uint32_t clear_screen_entry = 0x7D3B;
constexpr std::uint32_t roster_drawn_return = 0x148A;
constexpr std::uint32_t draw_string_entry = 0x076B6;

/// The string drawer's frame at its entry: the far return address on top,
/// then its five arguments, pushed deepest first — column, row, colour,
/// string segment, string offset — so the last one pushed is nearest SP.
/// The frame `docs/seams.md` §3 tabulates for calling it, read from the
/// other side.
constexpr std::uint16_t draw_frame_string_offset = 4;
constexpr std::uint16_t draw_frame_string_segment = 6;

/// The routine that draws the party roster, as a paragraph and an offset
/// rather than a flat image offset: it reaches its own literals as
/// `CS:<constant>`, so it only works when CS is the segment it was linked
/// at (`seam_automap.cpp` has what assuming otherwise cost). One argument,
/// a far pointer to the current member, and it cleans four bytes.
constexpr std::uint16_t roster_draw_paragraph = 0x0BA;
constexpr std::uint16_t roster_draw_offset = 0x0767;

/// The box-region clear as a call target rather than a place to stop: the
/// reader's own rect is cleared before the roster is drawn back over it,
/// because the drawer clears only its own rows.
constexpr auto image_clear_region =
    static_cast<std::uint16_t>(clear_region_entry);

/// Where the data segment begins, as an offset in the image.
constexpr std::uint32_t dgroup_offset = 0xC7C0;

// --- Offsets in the data segment -------------------------------------------

/// The game mode byte, and the three values that have a party roster on
/// the screen — which is the only thing this reader may draw over.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_camp = 2;
constexpr std::uint8_t mode_adventure_flat = 3;
constexpr std::uint8_t mode_adventure = 4;

/// The current party member: a far pointer, offset then segment. The one
/// argument the roster drawer takes.
constexpr std::uint16_t data_current_member = 0x5D92;

/// The program's own one-byte keyboard pushback slot: non-zero while the
/// second half of an extended key is waiting to be handed over.
constexpr std::uint16_t data_key_pushback = 0x8501;

/// The 8x8 font the program draws every menu and message with, as a far
/// pointer in the data segment: sixty-four glyphs of eight bytes, one byte
/// to a scanline, bit 0x80 the leftmost pixel, indexed by the character
/// upper-cased and taken modulo sixty-four. Two zero words is the
/// program's own "not installed yet", and this treats it the same way its
/// own text primitive does.
constexpr std::uint16_t data_font_pointer = 0x5E20;
constexpr std::uint16_t font_glyphs = 64;
constexpr std::uint16_t font_glyph_bytes = 8;
constexpr std::uint16_t font_bytes = font_glyphs * font_glyph_bytes;

/// The longest Pascal string this seam will copy out of the program to
/// look at. The program's own screen is forty characters wide, so a
/// string longer than one line of it is not a line of text it drew.
constexpr std::size_t longest_drawn_string = 64;

// ---------------------------------------------------------------------------
// The page, in the panel
// ---------------------------------------------------------------------------

/// The panel's geometry in the units this file draws in. The rect is
/// `automap.h`'s; what is here is how a page of text is laid out inside
/// it: a title row, twelve rows of body, and a row that names the key
/// that does the next thing.
constexpr int panel_width = static_cast<int>(automap_panel_width);
constexpr int panel_height = static_cast<int>(automap_panel_height);
constexpr int glyph_rows = static_cast<int>(font_glyph_bytes);
constexpr int glyph_columns = 8;
constexpr int reader_columns = panel_width / glyph_columns;
constexpr int reader_title_y = 0;
constexpr int reader_body_y = reader_title_y + glyph_rows;
constexpr int reader_body_rows = 12;
constexpr int reader_footer_y = reader_body_y + (reader_body_rows * glyph_rows);

static_assert(reader_footer_y + glyph_rows == panel_height,
              "the reader's rows have to fill the panel exactly");
static_assert(reader_columns == 22, "the panel is twenty-two glyphs wide");

/// How many pages of one entry the reader will count to. Four kilobytes
/// of text at 264 characters a page is sixteen; this is past any entry and
/// bounds the walk that counts them.
constexpr unsigned reader_max_pages = 64;

/// The colours, which are the program's own: the title in the yellow it
/// highlights with, the body in the green it writes messages in, and the
/// footer in grey so it reads as a label rather than as more of the text.
constexpr std::uint8_t colour_black = 0;
constexpr std::uint8_t colour_footer = 7;
constexpr std::uint8_t colour_body = 10;
constexpr std::uint8_t colour_title = 14;

// ---------------------------------------------------------------------------
// The command on the adventuring bar (M5-E4a, #221)
// ---------------------------------------------------------------------------
//
// F1 opens this reader and always has. What F1 is not is *discoverable*:
// a player looking at the adventuring screen sees six commands on a bar
// and no reason to believe a seventh exists. The game's own answer to
// "how do I do a thing" is a word on the bar, so the journal has one.
//
// This is `docs/seams.md` §3's mechanism and `seam_encamp_fix.cpp` is the
// worked example: the bars are Pascal strings in the data segment, handed
// to the program's own menu-bar input routine, and a seam that splices
// characters in before the bar goes out - and takes them back out when
// the routine returns - has added a command **the program draws**, in its
// own font and highlighting, and hands back like any other.
//
// Three rules go with it, and all three are facts here rather than hopes:
//
//  * **check the room.** The slot is a Pascal `string[40]`. The two bars
//    are thirty-three and twenty-seven characters, so six more fit on
//    either; the splice refuses rather than overruns when handed anything
//    else.
//  * **add a letter the program does not use.** The routine's command
//    letters are an *upper case only* class, and both bars are mixed
//    case - which is why each word draws with a large initial and a small
//    remainder. So the letters that select are the six initials, and the
//    `n` in the fourth word is lower case and selects nothing. `N` is
//    unreachable on both authentic bars.
//  * **splice, never compose.** Nothing below reads a word of the
//    program's bar or reproduces one. The item is this file's own six
//    characters, appended after the string's last, and the leading space
//    is its own separator - which is less than the Fix has to know, since
//    appending does not even have to find one.
//
// **The casing is not a preference.** One capital and a lower-case tail,
// exactly as `Fix` is. An all-caps item would make `O T E S` command
// letters too, and the routine's key scan does not stop at its first
// match - the *last* one wins - so a spliced capital `E` would quietly
// steal the fourth command.

/// The module the adventuring screen's input loop lives in: overlay 14,
/// whose facts the overlay tracker records (overlay.h) plus the word
/// below. The digest is of those bytes as read, so a copy whose overlay
/// file does not match is not this module.
constexpr std::uint32_t adventure_load_segment_at = 0x730;

constexpr seam_module adventure_module{
    .file = "GAME.OVR",
    .file_offset = 91851,
    .length = 4268,
    .digest =
        "ce2018e8e9d51d422d12e2a8e60837322af70639bcbca2b9fe34ccfd333e2d3a",
    .load_segment_at = adventure_load_segment_at};

/// The party's own two command bars, in the data segment: one for the
/// overhead view and one for the 3D view. The automap already tells this
/// pair from a vendor's, because every other bar in the game is a copy
/// built on the stack (M5-E2d) - these two offsets are the whole of that
/// distinction, and they are why a `Notes` command appears on the party's
/// screen and on nobody's shop.
constexpr std::uint16_t data_menu_area_bar = 0x04B6;
constexpr std::uint16_t data_menu_view_bar = 0x04DF;

/// The capacity of the slot either sits in: a Pascal `string[40]`, a
/// length byte and forty characters, which is also the width of the
/// screen in characters.
constexpr std::uint8_t menu_bar_capacity = 40;

/// In the adventuring input loop, the instruction that calls the menu-bar
/// routine, and the instruction it returns to - one pair per view mode.
/// Offsets from the start of the module above, which is to say from the
/// segment the manager has most recently put it at.
///
/// The second of each pair runs *before* the instruction that stores what
/// the routine answered, so the letter is still in AL.
constexpr std::uint32_t area_bar_before_input = 0x09D0;
constexpr std::uint32_t area_bar_after_input = 0x09D5;
constexpr std::uint32_t view_bar_before_input = 0x0C40;
constexpr std::uint32_t view_bar_after_input = 0x0C45;

/// Where the loop keeps the routine's out-parameter, below its own frame
/// pointer. Zero means "a letter was selected off the bar", which is the
/// only case this seam has any business in: anything else is a movement
/// key or a key the routine handled itself.
constexpr std::uint16_t frame_out_flag = 0x04;

/// The command, as it appears on the bar: a separator and the word, six
/// characters, **written here and nowhere read from the program**.
constexpr std::array<std::uint8_t, 6> notes_item{' ', 'N', 'o', 't', 'e', 's'};
constexpr unsigned notes_item_length = 6;

/// The letter the routine answers with when it is chosen.
constexpr std::uint8_t notes_key_ascii = 'N';

// ---------------------------------------------------------------------------
// Reading the machine
// ---------------------------------------------------------------------------

/// The data segment, if DS is where the fact table says it should be.
/// Zero — which is never a data segment, because that is the interrupt
/// vector table — when it is not, and every handler treats that as a
/// decline.
[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const auto expected =
      static_cast<std::uint16_t>((ctx.image_base() + dgroup_offset) / 16U);
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  return ds == expected ? ds : 0;
}

[[nodiscard]] std::uint16_t at(std::uint16_t base, std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;
};

[[nodiscard]] far_pointer far_at(cpu::processor& cpu, std::uint16_t segment,
                                 std::uint16_t offset) {
  return {.offset = cpu.read_word(segment, offset),
          .segment = cpu.read_word(segment, at(offset, 2))};
}

/// Whether a far pointer names conventional memory and can be followed for
/// `length` bytes. A pointer the program has not set up yet points
/// anywhere, and a read above conventional memory is a read of the video
/// window — which loads the adapter's latches, so a seam that wandered
/// there would be changing the machine to look at it.
[[nodiscard]] bool followable(const far_pointer& pointer,
                              std::uint32_t length) {
  if (pointer.segment == 0 || length == 0) {
    return false;
  }
  if (static_cast<std::uint32_t>(pointer.offset) + length > 0x10000U) {
    return false;
  }
  return cpu::physical_address(pointer.segment, pointer.offset) + length <=
         conventional_ram_size;
}

using font_table = std::array<std::uint8_t, font_bytes>;

/// The program's own glyphs, copied out of its memory through the bus.
/// False when the far pointer is not one that can be followed, which
/// covers the program's own "the font is not installed yet".
[[nodiscard]] bool read_font(cpu::processor& cpu, std::uint16_t ds,
                             font_table& font) {
  const far_pointer pointer = far_at(cpu, ds, data_font_pointer);
  if (!followable(pointer, font_bytes)) {
    return false;
  }
  for (std::uint16_t i = 0; i < font_bytes; ++i) {
    font[i] = cpu.read_byte(pointer.segment, at(pointer.offset, i));
  }
  return true;
}

/// Whether the screen the program is showing has a party roster on it,
/// which is the only screen whose right-hand panel is this seam's to take
/// and — the part that matters — to give back.
[[nodiscard]] bool has_roster(cpu::processor& cpu, std::uint16_t ds) {
  const std::uint8_t mode = cpu.read_byte(ds, data_game_mode);
  return mode == mode_camp || mode == mode_adventure_flat ||
         mode == mode_adventure;
}

// ---------------------------------------------------------------------------
// Laying a page out
// ---------------------------------------------------------------------------

/// One page of wrapped text: up to `reader_body_rows` lines, where the
/// text after them begins, and whether there is any.
struct page_layout {
  std::array<std::string_view, reader_body_rows> line{};
  unsigned lines{};
  std::size_t next{};
  bool more{false};
};

[[nodiscard]] constexpr bool is_space(char ch) noexcept {
  return ch == ' ' || ch == '\t' || ch == '\r';
}

/// Greedy word wrap from `start`, at most one page of it.
///
/// Breaks at spaces; a word longer than the panel is wide is broken where
/// it runs out of columns, because a word that cannot fit has to go
/// somewhere and dropping it would be losing the player's own text. A
/// newline ends a line, and a second one in a row leaves a blank — which
/// is what a paragraph break in an OCR engine's output looks like.
[[nodiscard]] page_layout lay_out(std::string_view text, std::size_t start) {
  page_layout page;
  std::size_t p = std::min(start, text.size());
  while (page.lines < reader_body_rows && p < text.size()) {
    while (p < text.size() && is_space(text[p])) {
      ++p;
    }
    if (p >= text.size()) {
      break;
    }
    if (text[p] == '\n') {
      ++p;
      page.line[page.lines++] = std::string_view{};
      continue;
    }

    std::size_t q = p;
    std::size_t last_space = text.size();
    int taken = 0;
    while (q < text.size() && text[q] != '\n' && taken < reader_columns) {
      if (is_space(text[q])) {
        last_space = q;
      }
      ++q;
      ++taken;
    }

    std::size_t end = q;
    std::size_t next = q;
    if (q < text.size() && text[q] != '\n' && taken == reader_columns &&
        !is_space(text[q])) {
      // Mid-word at the right-hand edge: back up to the last space if the
      // line has one, and break the word where it stands if it has not.
      if (last_space != text.size() && last_space > p) {
        end = last_space;
        next = last_space + 1;
      }
    } else if (q < text.size() && text[q] == '\n') {
      next = q + 1;
    }
    while (end > p && is_space(text[end - 1])) {
      --end;
    }
    page.line[page.lines++] = text.substr(p, end - p);
    p = next;
  }

  while (p < text.size() && (is_space(text[p]) || text[p] == '\n')) {
    ++p;
  }
  page.next = p;
  page.more = p < text.size();
  return page;
}

/// Where page `wanted` begins, and how many pages there turn out to be.
struct page_walk {
  std::size_t start{};
  unsigned count{1};
};

[[nodiscard]] page_walk walk_pages(std::string_view text, unsigned wanted) {
  page_walk walk;
  std::size_t at_byte = 0;
  std::size_t last_start = 0;
  unsigned page = 0;
  bool found = false;
  for (;;) {
    if (page == wanted) {
      walk.start = at_byte;
      found = true;
    }
    last_start = at_byte;
    const page_layout laid = lay_out(text, at_byte);
    if (!laid.more || page + 1 >= reader_max_pages) {
      walk.count = page + 1;
      break;
    }
    at_byte = laid.next;
    ++page;
  }
  if (!found) {
    // Asked for a page past the end — which the reader does not do, but a
    // shorter entry delivered under an older page number would. The last
    // page is the honest answer.
    walk.start = last_start;
  }
  return walk;
}

// ---------------------------------------------------------------------------
// Drawing it, into the seam's own buffer
// ---------------------------------------------------------------------------

using panel_pixels = std::array<std::uint8_t, automap_panel_pixels>;

void put(panel_pixels& panel, int x, int y, std::uint8_t colour) noexcept {
  if (x < 0 || y < 0 || x >= panel_width || y >= panel_height) {
    return;
  }
  panel[(static_cast<std::size_t>(y) * automap_panel_width) +
        static_cast<std::size_t>(x)] = colour;
}

/// One run of text into the panel, in the program's own glyphs.
///
/// Rasterized here, into this seam's own linear buffer, rather than by
/// calling the program's text primitive: the screen is planar and the
/// program's, the panel is linear and this seam's, and the panel goes onto
/// the planes in one piece. The glyphs are the same bytes the program
/// draws its own menus with, so a page of a journal is pixel-identical to
/// the text around it — which is what "in the game's own font" has to mean
/// to be worth claiming. It is `seam_automap.cpp`'s label writer, and the
/// two are the same three lines for the same reason.
void draw_text(panel_pixels& panel, int x, int y, std::string_view text,
               std::uint8_t colour, const font_table& font) noexcept {
  for (const char ch : text) {
    auto code = static_cast<std::uint8_t>(ch);
    if (code >= 0x61 && code <= 0x7A) {
      code = static_cast<std::uint8_t>(code - 0x20);
    }
    const auto glyph = static_cast<std::size_t>(code % font_glyphs) *
                       static_cast<std::size_t>(font_glyph_bytes);
    for (int row = 0; row < glyph_rows; ++row) {
      const std::uint8_t bits = font[glyph + static_cast<std::size_t>(row)];
      for (int bit = 0; bit < glyph_columns; ++bit) {
        if (((bits >> (glyph_columns - 1 - bit)) & 1U) != 0) {
          put(panel, x + bit, y + row, colour);
        }
      }
    }
    x += glyph_columns;
  }
}

/// The same, centred in the panel — on a **character cell**, not on a
/// pixel. The program sets all of its own text on that grid, and a line
/// half a glyph out of it is the one thing on this panel that would read
/// as foreign however right the glyphs were.
void draw_centred(panel_pixels& panel, int y, std::string_view text,
                  std::uint8_t colour, const font_table& font) noexcept {
  const auto columns = static_cast<int>(std::min<std::size_t>(
      text.size(), static_cast<std::size_t>(reader_columns)));
  draw_text(panel, ((reader_columns - columns) / 2) * glyph_columns, y,
            text.substr(0, static_cast<std::size_t>(columns)), colour, font);
}

/// What the panel calls each of the journal's sections.
///
/// Separate from `machine::journal_kind_name()`, which is the lower-case
/// token a store file and a log line use. This is a *caption*: upper case
/// because that is the case the program's font is legible in, and short
/// enough that it and a four-digit number fit the panel's twenty-two
/// columns — which `PROCLAMATION 214` does, with five to spare.
[[nodiscard]] constexpr std::string_view reader_word(
    journal_kind which) noexcept {
  switch (which) {
    case journal_kind::entry:
      return "ENTRY";
    case journal_kind::tale:
      return "TALE";
    case journal_kind::proclamation:
      return "PROCLAMATION";
  }
  return "ENTRY";
}

/// A short line built out of this file's own characters — a title, a
/// footer, a prompt. Never a word of the program's, which is why it is a
/// fixed buffer rather than a pointer into the machine.
class label {
 public:
  void add(std::string_view what) noexcept {
    for (const char ch : what) {
      if (length_ < text_.size()) {
        text_[length_++] = ch;
      }
    }
  }

  void add(unsigned value) noexcept {
    std::array<char, 5> digits{};
    std::size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    } while (value != 0 && count < digits.size());
    while (count > 0) {
      if (length_ < text_.size()) {
        text_[length_++] = digits[--count];
      } else {
        --count;
      }
    }
  }

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view{text_.data(), length_};
  }

 private:
  std::array<char, reader_columns> text_{};
  std::size_t length_{};
};

/// What the reader says instead of a page when the host had nothing.
/// Two short lines, this file's own words, and the second is what a
/// player would do about it.
struct refusal {
  std::string_view first;
  std::string_view second;
};

[[nodiscard]] refusal refusal_for(journal_delivery why) noexcept {
  switch (why) {
    case journal_delivery::no_host:
    case journal_delivery::no_journal:
      return {.first = "NO JOURNAL", .second = "HAS BEEN READ"};
    case journal_delivery::no_entry:
      return {.first = "NO SUCH ENTRY", .second = "IN THIS JOURNAL"};
    case journal_delivery::no_text:
      return {.first = "NOTHING WAS READ", .second = "FROM THAT ENTRY"};
    case journal_delivery::none:
    case journal_delivery::ready:
      break;
  }
  return {.first = {}, .second = {}};
}

/// The whole panel into its own buffer, and how many pages the entry
/// turned out to have.
[[nodiscard]] unsigned render(journal_state& state, const font_table& font) {
  panel_pixels& panel = state.pixels();
  panel.fill(colour_black);

  if (state.reader() == journal_reader_mode::asking) {
    label title;
    title.add("JOURNAL");
    draw_centred(panel, reader_title_y, title.view(), colour_title, font);

    label prompt;
    prompt.add(reader_word(state.asked_kind()));
    prompt.add(" ");
    prompt.add(state.digits());
    // The cursor is **drawn**, not lettered, and that is a fact about the
    // program's font rather than a preference. Its table is sixty-four
    // glyphs indexed by the character modulo sixty-four, and driven
    // against the program an underscore came out as a stray mark: the
    // index it lands on is not one the program has ever needed. A rule
    // under the next cell is this seam's own pixels and cannot be
    // surprised by a glyph nobody drew. One cell is left for it in the
    // centring, so the prompt does not shuffle as digits are typed.
    const auto columns = static_cast<int>(prompt.view().size());
    const int prompt_x = ((reader_columns - (columns + 1)) / 2) * glyph_columns;
    const int prompt_y = reader_body_y + (4 * glyph_rows);
    draw_text(panel, prompt_x, prompt_y, prompt.view(), colour_body, font);
    for (int x = 1; x < glyph_columns - 1; ++x) {
      put(panel, prompt_x + (columns * glyph_columns) + x,
          prompt_y + glyph_rows - 2, colour_body);
    }

    // The prompt has to say which of the three sections it is pointed at
    // and how to point it elsewhere, because a number alone names three
    // different texts (`machine/journal.h`'s `journal_kind`). The word
    // above *is* the answer to the first, so this line is only the second.
    label hint;
    hint.add("F1 PICKS SECTION");
    draw_centred(panel, reader_body_y + (6 * glyph_rows), hint.view(),
                 colour_footer, font);

    label footer;
    footer.add("RETURN OPENS IT");
    draw_centred(panel, reader_footer_y, footer.view(), colour_footer, font);
    return 1;
  }

  label title;
  title.add(reader_word(state.entry().kind));
  title.add(" ");
  title.add(state.entry().number);
  draw_centred(panel, reader_title_y, title.view(), colour_title, font);

  if (state.delivery() != journal_delivery::ready) {
    const refusal what = refusal_for(state.delivery());
    draw_centred(panel, reader_body_y + (4 * glyph_rows), what.first,
                 colour_body, font);
    draw_centred(panel, reader_body_y + (5 * glyph_rows), what.second,
                 colour_body, font);
    label footer;
    footer.add("ESC CLOSES");
    draw_centred(panel, reader_footer_y, footer.view(), colour_footer, font);
    return 1;
  }

  const std::string_view text = state.text();
  const page_walk walk = walk_pages(text, state.page());
  const page_layout laid = lay_out(text, walk.start);
  for (unsigned row = 0; row < laid.lines; ++row) {
    draw_text(panel, 0, reader_body_y + (static_cast<int>(row) * glyph_rows),
              laid.line[row], colour_body, font);
  }

  label footer;
  if (walk.count > 1) {
    footer.add(state.page() + 1U);
    footer.add("/");
    footer.add(walk.count);
    footer.add("  ");
  }
  footer.add(laid.more ? "F1 MORE" : "F1 CLOSES");
  if (!laid.more && state.truncated()) {
    // The entry was longer than the buffer that crossed the host boundary
    // (journal.h). Said rather than silently stopped: a transcription with
    // a hole in it that nothing mentions is the failure a player finds out
    // about last.
    footer.add(" +");
  }
  draw_centred(panel, reader_footer_y, footer.view(), colour_footer, font);
  return walk.count;
}

// ---------------------------------------------------------------------------
// Putting it on the planes
// ---------------------------------------------------------------------------
//
// `docs/seams.md` §3's eighth primitive, port surgery, exactly as
// `seam_automap.cpp` uses it and for the same reason: a byte written into
// the video window with the map mask the program leaves behind lands in
// all four planes at once, so a panel drawn that way could be black and
// white and nothing else. The registers a write mode 0 copy depends on are
// set rather than assumed — they cannot be read back — and the resting
// state the program's own drawing primitives leave is the state this hands
// back.

constexpr std::uint8_t gc_enable_set_reset_index = 1;
constexpr std::uint8_t gc_data_rotate_index = 3;
constexpr std::uint8_t gc_write_mode_index = 5;
constexpr std::uint8_t gc_bit_mask_index = 8;
constexpr std::uint8_t sequencer_map_mask_index = 2;
constexpr std::uint8_t all_planes = 0x0F;
constexpr std::uint8_t all_bits = 0xFF;

/// Bytes per scanline of one plane in the 320-pixel graphics mode the
/// program runs in, and the segment of the window it lands in.
constexpr std::uint16_t plane_bytes_per_row = 40;
constexpr std::uint16_t video_window_segment = 0xA000;

void write_register(machine& box, std::uint16_t index_port,
                    std::uint16_t data_port, std::uint8_t index,
                    std::uint8_t value) {
  box.write_port8(index_port, index);
  box.write_port8(data_port, value);
}

void blit(machine& box, const journal_state& state) {
  const panel_pixels& panel = state.pixels();
  cpu::processor& cpu = box.processor();

  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);

  constexpr std::uint16_t bytes_across = automap_panel_width / 8;
  constexpr std::uint16_t first_byte_column = automap_panel_x / 8;

  for (std::uint8_t plane = 0; plane < ega::plane_count; ++plane) {
    write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                   sequencer_map_mask_index,
                   static_cast<std::uint8_t>(1U << plane));
    for (std::uint16_t row = 0; row < automap_panel_height; ++row) {
      const auto line = static_cast<std::uint16_t>(
          ((automap_panel_y + row) * plane_bytes_per_row) + first_byte_column);
      const std::size_t source =
          static_cast<std::size_t>(row) * automap_panel_width;
      for (std::uint16_t column = 0; column < bytes_across; ++column) {
        std::uint8_t bits = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
          const std::uint8_t colour =
              panel[source + (static_cast<std::size_t>(column) * 8) + bit];
          if (((colour >> plane) & 1U) != 0) {
            bits = static_cast<std::uint8_t>(bits | (0x80U >> bit));
          }
        }
        cpu.write_byte(video_window_segment, at(line, column), bits);
      }
    }
  }

  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);
}

/// Put the party roster back, because the reader wrote over it and only
/// the program can redraw it from live state.
///
/// Two calls in one batch, exactly as the automap closes: the panel's rect
/// through the program's own region clear — the drawer clears only the
/// rows it fills, so the row above the header and the rows below the party
/// would keep their pixels — and then the drawer itself. The reader is
/// marked down *before* the batch is queued, because when a batch finishes
/// the engine offers the point again and a handler that had not already
/// recorded what it was doing would queue the same calls a second time
/// (#188).
void give_the_roster_back(machine& box, seam_context& ctx, std::uint16_t ds) {
  journal_state& state = box.journal();
  state.set_on_screen(false);
  state.set_drawn_signature(0);

  cpu::processor& cpu = box.processor();
  if (!has_roster(cpu, ds)) {
    // The program's own rule: the roster is only there to be redrawn on
    // the modes that have one. A repaint the program cannot perform would
    // leave a corrupted screen, which is a worse answer than a stale one.
    return;
  }

  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::array<std::uint16_t, 4> clear{
      automap_panel_left_col, automap_panel_top_row, automap_panel_right_col,
      automap_panel_bottom_row};
  const std::array<std::uint16_t, 2> current{
      cpu.read_word(ds, at(data_current_member, 2)),
      cpu.read_word(ds, data_current_member)};
  (void)(ctx.call_program(image, image_clear_region, clear) &&
         ctx.call_program(
             static_cast<std::uint16_t>(image + roster_draw_paragraph),
             roster_draw_offset, current));
}

// ---------------------------------------------------------------------------
// The keys
// ---------------------------------------------------------------------------

/// F1, as the BIOS hands it over: the scan code in the high byte and no
/// character at all, which is what makes it nobody else's (the header).
constexpr std::uint16_t key_f1 = 0x3B00;
constexpr std::uint16_t key_escape = 0x011B;
constexpr std::uint16_t key_backspace = 0x0E08;
constexpr std::uint16_t key_return = 0x1C0D;

/// What the keystroke at the head of the buffer is, to this seam.
enum class claimable : std::uint8_t {
  /// Somebody else's key. Every key is this one, nearly always.
  none,
  /// F1: open the prompt, turn a page, or put the entry away.
  reader,
  /// Escape: put it away, from wherever it is.
  close,
  /// Backspace: a page back, or a digit rubbed out.
  back,
  /// Return: open the entry the prompt names.
  accept,
  /// A digit at the prompt.
  digit,
};

[[nodiscard]] claimable claimable_of(std::uint16_t key,
                                     journal_reader_mode mode) noexcept {
  if (key == key_f1) {
    return claimable::reader;
  }
  if (mode == journal_reader_mode::closed) {
    // With the reader down, F1 is the only key in the world that is this
    // seam's. Everything below is the modal claim, and it lasts exactly as
    // long as the reader is the thing on the screen.
    return claimable::none;
  }
  if (key == key_escape) {
    return claimable::close;
  }
  if (key == key_backspace) {
    return claimable::back;
  }
  if (mode == journal_reader_mode::asking) {
    if (key == key_return) {
      return claimable::accept;
    }
    const auto character = static_cast<std::uint8_t>(key & 0xFFU);
    if (character >= '0' && character <= '9') {
      return claimable::digit;
    }
  }
  return claimable::none;
}

/// Take the next keystroke out of the BIOS buffer if it is one this seam
/// wants **right now** — which is the whole of the fidelity argument: a
/// key the seam is not going to act on is left exactly where the program
/// would have found it.
///
/// The three things it is careful about are the automap's three, for the
/// same reasons: only the head of the ring so keys keep their order and
/// their count, never while the program's own pushback slot is armed
/// because the two halves of an extended key have to stay adjacent, and
/// the whole keystroke word rather than the character.
[[nodiscard]] claimable claim_key(cpu::processor& cpu, std::uint16_t ds,
                                  journal_reader_mode mode,
                                  std::uint16_t& taken) {
  if (cpu.read_byte(ds, data_key_pushback) != 0) {
    return claimable::none;
  }
  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  const std::uint16_t tail =
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
  if (head == tail) {
    return claimable::none;
  }
  const std::uint16_t key = cpu.read_word(bda::segment, head);
  const claimable which = claimable_of(key, mode);
  if (which == claimable::none) {
    return claimable::none;
  }

  auto next = static_cast<std::uint16_t>(head + 2U);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  cpu.write_word(bda::segment, bda::keyboard_buffer_head, next);
  taken = key;
  return which;
}

// ---------------------------------------------------------------------------
// Opening, paging and closing
// ---------------------------------------------------------------------------

/// The log has changed; a host may want to write it down.
///
/// Called only when something actually moved, which is what the flag on
/// the log is for: a seam that called out on every citation would have a
/// host rewriting its file for a line that was already at the top.
void tell_the_host_the_log_moved(machine& box, seam_context& ctx) {
  if (!box.journal().seen_changed()) {
    return;
  }
  (void)ctx.call_host(seam_host_service::journal_seen, 0);
}

/// Ask the host for an entry. What it answered is in `journal_state`
/// afterwards, whichever way it went — a callout nothing served leaves
/// `no_host`, which `ask()` put there before the call (journal.h).
void request(machine& box, seam_context& ctx, journal_citation what) {
  box.journal().ask(what);
  (void)ctx.call_host(seam_host_service::journal_open,
                      journal_open_argument(what));
  // Opening it is what takes the `*` off its line (#222). Only a line the
  // log already has: an entry the player asked for at the prompt was
  // never cited, so there is nothing to mark and nothing to write down.
  if (box.journal().mark_seen_read(what)) {
    tell_the_host_the_log_moved(box, ctx);
  }
}

void close_reader(machine& box, seam_context& ctx, std::uint16_t ds) {
  journal_state& state = box.journal();
  const bool was_up = state.on_screen();
  state.set_reader(journal_reader_mode::closed);
  state.clear_digits();
  if (was_up) {
    give_the_roster_back(box, ctx, ds);
  }
}

// ---------------------------------------------------------------------------
// The bar splice
// ---------------------------------------------------------------------------

/// Where the command sits in `bar` right now: the one-based index of its
/// separator, or zero if it is not there.
///
/// Both the test for "is it already spliced in" and the answer to "where
/// do I take it out from", which is why it is one function.
[[nodiscard]] unsigned find_notes(cpu::processor& cpu, std::uint16_t ds,
                                  std::uint16_t bar, std::uint8_t length) {
  if (length < notes_item_length) {
    return 0;
  }
  for (unsigned index = 1; index + notes_item_length - 1 <= length; ++index) {
    bool all = true;
    for (unsigned nth = 0; nth < notes_item_length && all; ++nth) {
      all =
          cpu.read_byte(ds, at(bar, static_cast<std::uint16_t>(index + nth))) ==
          notes_item[nth];
    }
    if (all) {
      return index;
    }
  }
  return 0;
}

/// Put the command on `bar`, after its last. False and nothing written if
/// the string is not the shape the facts say - empty, longer than the slot
/// it sits in, too long to take six more characters, or already carrying
/// this command.
///
/// **Appended rather than inserted**, which is the one place this departs
/// from `seam_encamp_fix.cpp`. The Fix inserts before its bar's last
/// command because that bar ends with the way out of the screen and a
/// command after it would read oddly; this bar has no such item, the
/// mock-up this was designed from puts the new word at the end, and
/// appending has to know even less about the program's string than
/// inserting does - it never looks for a separator, because the item
/// carries its own.
[[nodiscard]] bool splice_in(cpu::processor& cpu, std::uint16_t ds,
                             std::uint16_t bar) {
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (length == 0 || length > menu_bar_capacity ||
      length + notes_item_length > menu_bar_capacity) {
    return false;
  }
  if (find_notes(cpu, ds, bar, length) != 0) {
    return false;  // already there: this pass is not the first.
  }
  for (unsigned nth = 0; nth < notes_item_length; ++nth) {
    cpu.write_byte(ds, at(bar, static_cast<std::uint16_t>(length + 1U + nth)),
                   notes_item[nth]);
  }
  cpu.write_byte(ds, bar,
                 static_cast<std::uint8_t>(length + notes_item_length));
  return true;
}

/// Take it back out, leaving the program's own string exactly as it was.
/// False if it was not there, which is not an error: the pass that could
/// not splice it in is the pass that has nothing to take out.
[[nodiscard]] bool splice_out(cpu::processor& cpu, std::uint16_t ds,
                              std::uint16_t bar) {
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (length > menu_bar_capacity) {
    return false;
  }
  const unsigned index = find_notes(cpu, ds, bar, length);
  if (index == 0) {
    return false;
  }
  // Everything after the item moves down over it. Appending means there is
  // normally nothing to move, but taking it out from wherever it is found
  // costs one loop and does not care how it got there.
  for (unsigned nth = index + notes_item_length; nth <= length; ++nth) {
    const std::uint8_t byte =
        cpu.read_byte(ds, at(bar, static_cast<std::uint16_t>(nth)));
    cpu.write_byte(
        ds, at(bar, static_cast<std::uint16_t>(nth - notes_item_length)), byte);
  }
  cpu.write_byte(ds, bar,
                 static_cast<std::uint8_t>(length - notes_item_length));
  return true;
}

/// F1, wherever the reader happens to be.
///
/// One key that opens the prompt, points it at each section in turn,
/// turns the pages and puts the entry away on the last of them. It is the
/// whole surface a player has to learn, and the panel says what it will do
/// next every time it is on the screen.
///
/// **The section chooser is this key and not another one** (#218). The
/// prompt needs one — a player typing `4` has not said which section they
/// mean — and every key this seam might have taken instead is a key some
/// other seam may want: the automap's is Tab, and two enhancements a
/// player has both switched on must not fight over a keystroke. Escape is
/// what leaves the prompt, and always was.
void press_reader_key(machine& box, seam_context& ctx, std::uint16_t ds) {
  journal_state& state = box.journal();
  switch (state.reader()) {
    case journal_reader_mode::closed:
      state.clear_digits();
      state.set_asked_kind(journal_kind::entry);
      state.set_reader(journal_reader_mode::asking);
      return;
    case journal_reader_mode::asking:
      state.cycle_asked_kind();
      return;
    case journal_reader_mode::showing:
      break;
  }
  if (state.page_count() == 0) {
    // Nothing has been drawn yet, so there is no page to turn and no
    // picture to put away. Reachable only in the one step between a
    // citation opening the reader and the arrival that draws it.
    return;
  }
  if (state.page() + 1U < state.page_count()) {
    state.set_page(static_cast<std::uint16_t>(state.page() + 1U));
    return;
  }
  close_reader(box, ctx, ds);
}

/// Everything one arrival does with the keyboard. True when the roster is
/// on its way back through a batch, which is the caller's cue that it is
/// finished for this pass.
[[nodiscard]] bool handle_keys(machine& box, seam_context& ctx,
                               std::uint16_t ds) {
  journal_state& state = box.journal();
  std::uint16_t key = 0;
  switch (claim_key(box.processor(), ds, state.reader(), key)) {
    case claimable::none:
      return false;
    case claimable::reader:
      press_reader_key(box, ctx, ds);
      return state.reader() == journal_reader_mode::closed;
    case claimable::close:
      close_reader(box, ctx, ds);
      return true;
    case claimable::back:
      if (state.reader() == journal_reader_mode::asking) {
        state.pop_digit();
      } else if (state.page() != 0) {
        state.set_page(static_cast<std::uint16_t>(state.page() - 1U));
      }
      return false;
    case claimable::accept:
      if (const journal_citation wanted = state.asked(); wanted) {
        request(box, ctx, wanted);
        state.set_reader(journal_reader_mode::showing);
        state.set_page(0);
      }
      return false;
    case claimable::digit:
      (void)state.push_digit(static_cast<char>(key & 0xFFU));
      return false;
  }
  return false;
}

/// Draw the reader if it is up, is not covered, and what would be drawn is
/// not already there.
void draw_if_wanted(machine& box, seam_context& ctx, std::uint16_t ds) {
  journal_state& state = box.journal();
  cpu::processor& cpu = box.processor();
  if (state.reader() == journal_reader_mode::closed || state.covered() ||
      !has_roster(cpu, ds)) {
    return;
  }

  // Everything the panel is drawn from, as one number. The font pointer is
  // in it so a reader first drawn before the program installed its glyphs
  // gets them the moment it does.
  std::uint32_t drawn = 2166136261U;
  const auto mix = [&drawn](std::uint32_t value) noexcept {
    drawn = (drawn ^ value) * 16777619U;
    drawn ^= drawn >> 13U;
  };
  mix(static_cast<std::uint32_t>(state.reader()));
  mix(journal_open_argument(state.entry()));
  mix(state.page());
  mix(static_cast<std::uint32_t>(state.delivery()));
  mix(static_cast<std::uint32_t>(state.digits().size()));
  // The prompt's *pair*: pointing it at another section changes what is
  // drawn without changing a digit, and a signature that mixed only the
  // number would decide the panel was already right.
  mix(journal_open_argument(state.asked()));
  mix(cpu.read_word(ds, at(data_font_pointer, 2)));
  if (drawn == 0) {
    // Zero is this seam's "nothing has been drawn" (journal.h), so it is
    // not allowed to be a real answer.
    drawn = 1;
  }
  if (drawn == state.drawn_signature() && state.on_screen()) {
    return;
  }

  font_table font{};
  if (!read_font(cpu, ds, font)) {
    // The program has not installed its glyphs. A page rasterized out of
    // an empty buffer is a black rectangle, and black is what this panel
    // draws nothing in — so nothing is drawn, and the next arrival that
    // finds a font draws then.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  state.set_page_count(static_cast<std::uint16_t>(render(state, font)));
  blit(box, state);
  state.set_on_screen(true);
  state.set_drawn_signature(drawn);
}

// ---------------------------------------------------------------------------
// The handlers
// ---------------------------------------------------------------------------

/// The workhorse: the program is asking whether a key is waiting, which is
/// where it is between commands.
void at_key_pending(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!has_roster(cpu, ds)) {
    // Not a screen this reader can be on, so **its keys are nobody's
    // here**: a key claimed where nothing can be drawn is a key the
    // player pressed and saw no answer to. An entry a citation opened on
    // such a screen is not closed, only unrendered — it comes up when a
    // screen with a roster does, which is the same rule the covered-cells
    // test follows one step in.
    return;
  }
  if (handle_keys(box, ctx, ds)) {
    // The roster is on its way back through a batch. Nothing else this
    // pass.
    return;
  }
  draw_if_wanted(box, ctx, ds);
}

/// The program is about to wait for a key.
///
/// This one **draws too**, unlike the automap's point at the same address,
/// and the difference is the reason the reader exists. The automap has
/// nothing to draw at a blocking wait because the party cannot have moved.
/// The reader can have been opened by a citation the program drew a
/// moment ago — and then the program waits, inside the BIOS, and the
/// poll point is not reached again until a key arrives. A reader that only
/// drew at the poll would appear when the player pressed something, which
/// is one press too late.
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!has_roster(cpu, ds)) {
    return;
  }
  if (handle_keys(box, ctx, ds)) {
    return;
  }
  draw_if_wanted(box, ctx, ds);
}

/// The program is about to draw a string. Is it citing an entry?
void at_draw_string(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const far_pointer where{
      .offset = cpu.read_word(ss, at(sp, draw_frame_string_offset)),
      .segment = cpu.read_word(ss, at(sp, draw_frame_string_segment))};
  if (!followable(where, 1)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint8_t length = cpu.read_byte(where.segment, where.offset);
  if (length == 0) {
    return;
  }
  const auto take = static_cast<std::uint8_t>(
      std::min<std::size_t>(length, longest_drawn_string));
  if (!followable(where, static_cast<std::uint32_t>(take) + 1U)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  std::array<char, longest_drawn_string> drawn{};
  for (std::uint8_t i = 0; i < take; ++i) {
    drawn[i] = static_cast<char>(cpu.read_byte(
        where.segment, at(where.offset, static_cast<std::uint16_t>(i + 1))));
  }

  journal_state& state = box.journal();
  const journal_citation cited =
      state.note_drawn_text(std::string_view{drawn.data(), take});
  if (!cited) {
    return;
  }

  // Into the log first, with the moment the game said it: the machine's
  // own seeded wall clock, which is the host's instant plus the virtual
  // time since (`machine/platform.h`). Derived rather than read, so
  // nothing here goes near the host's clock and a replay gets the same
  // answer twice.
  const wall_time when = box.wall().at(box.time());
  state.note_seen(cited, when.month, when.day, when.hour, when.minute);
  tell_the_host_the_log_moved(box, ctx);

  // A citation, and the whole enhancement: the entry opens. The reader is
  // not moved onto an entry the host has nothing for — "the seam shows
  // nothing rather than a blank page" (#175) — unless it is already open,
  // in which case the game has just cited something else and saying so
  // beats leaving the previous entry up as though it were the answer.
  const bool was_open = state.reader() != journal_reader_mode::closed;
  if (was_open && state.reader() == journal_reader_mode::showing &&
      state.entry() == cited && state.delivery() == journal_delivery::ready) {
    return;
  }
  request(box, ctx, cited);
  if (state.delivery() == journal_delivery::ready || was_open) {
    state.set_reader(journal_reader_mode::showing);
    state.set_page(0);
  }
}

/// A box region is about to be cleared. If it meets the panel's cells,
/// something else is taking the screen there.
void at_clear_region(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];

  // At the routine's entry the stack holds its far return address and then
  // its four arguments, each a word whose low byte is the value: bottom,
  // right, top, left, in the order the program's own callers push them.
  constexpr std::uint16_t frame_bottom = 4;
  constexpr std::uint16_t frame_right = 6;
  constexpr std::uint16_t frame_top = 8;
  constexpr std::uint16_t frame_left = 10;

  const auto bottom = cpu.read_byte(ss, at(sp, frame_bottom));
  const auto right = cpu.read_byte(ss, at(sp, frame_right));
  const auto top = cpu.read_byte(ss, at(sp, frame_top));
  const auto left = cpu.read_byte(ss, at(sp, frame_left));
  if (bottom < top || right < left) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (automap_state::rect_meets_panel(bottom, right, top, left)) {
    box.journal().set_covered(true);
  }
}

/// The whole screen is about to be cleared, which certainly includes these
/// cells.
void at_clear_screen(machine& box, seam_context& ctx) {
  (void)ctx;
  box.journal().set_covered(true);
}

/// The party roster is on the screen again: these cells are the reader's
/// to claim once more.
void at_roster_drawn(machine& box, seam_context& ctx) {
  (void)ctx;
  box.journal().set_covered(false);
}

// ---------------------------------------------------------------------------
// The definition
// ---------------------------------------------------------------------------

/// A command bar is about to go out: put the command on it.
///
/// Declines quietly on anything unexpected - a data segment that is not
/// where the fact table says, a string that is not the shape the facts
/// say - and the player sees the game's own bar, which is the failure
/// this mechanism is supposed to have (`docs/seams.md` §2).
void bar_before(machine& box, seam_context& ctx, std::uint16_t bar) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    return;
  }
  static_cast<void>(splice_in(cpu, ds, bar));
}

/// The bar has come back with a letter. Take the command off it, and if
/// the letter is this seam's, open the reader.
///
/// The splice comes out **unconditionally and first**: outside the one
/// call that drew it, the program's string is the program's string, byte
/// for byte. Everything after that is allowed to decline.
///
/// A letter this seam does not own is left entirely alone. So is this
/// seam's own letter when the reader is already up - the player can see
/// the reader, and the way out of it is the way out of it. And the
/// program is *never* stopped from seeing the `N`: it compares what came
/// back against its own commands, matches none of them, and goes round
/// the loop again, which is what makes adding a letter safe at all.
void bar_after(machine& box, seam_context& ctx, std::uint16_t bar) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    return;
  }
  static_cast<void>(splice_out(cpu, ds, bar));

  const cpu::registers& regs = cpu.regs();
  const std::uint8_t out_flag = cpu.read_byte(
      regs[cpu::sreg::ss],
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - frame_out_flag));
  if (out_flag != 0 || regs.get(cpu::reg8::al) != notes_key_ascii) {
    return;
  }
  journal_state& state = box.journal();
  if (state.reader() != journal_reader_mode::closed) {
    return;
  }
  state.clear_digits();
  state.set_asked_kind(journal_kind::entry);
  state.set_reader(journal_reader_mode::asking);
}

void at_area_bar_before(machine& box, seam_context& ctx) {
  bar_before(box, ctx, data_menu_area_bar);
}

void at_area_bar_after(machine& box, seam_context& ctx) {
  bar_after(box, ctx, data_menu_area_bar);
}

void at_view_bar_before(machine& box, seam_context& ctx) {
  bar_before(box, ctx, data_menu_view_bar);
}

void at_view_bar_after(machine& box, seam_context& ctx) {
  bar_after(box, ctx, data_menu_view_bar);
}

constexpr std::array<seam_point, 10> journal_points{
    {{.module = resident_image,
      .offset = key_pending_entry,
      .run = &at_key_pending},
     {.module = resident_image, .offset = key_read_entry, .run = &at_key_read},
     {.module = resident_image,
      .offset = draw_string_entry,
      .run = &at_draw_string},
     {.module = resident_image,
      .offset = clear_region_entry,
      .run = &at_clear_region},
     {.module = resident_image,
      .offset = clear_screen_entry,
      .run = &at_clear_screen},
     {.module = resident_image,
      .offset = roster_drawn_return,
      .run = &at_roster_drawn},
     // The four that put `Notes` on the party's own bar (#221). A pair per
     // view mode, and both pairs are in the adventuring loop's own module
     // rather than the resident image - which is what makes the splice
     // come out in the same call that drew it.
     {.module = adventure_module,
      .offset = area_bar_before_input,
      .run = &at_area_bar_before},
     {.module = adventure_module,
      .offset = area_bar_after_input,
      .run = &at_area_bar_after},
     {.module = adventure_module,
      .offset = view_bar_before_input,
      .run = &at_view_bar_before},
     {.module = adventure_module,
      .offset = view_bar_after_input,
      .run = &at_view_bar_after}}};

/// **Ungated**, and that is a decision rather than an omission.
///
/// A journal-gated seam would be inert for every player alive: a gate is
/// satisfied by a document whose fingerprint is in `known_documents()`,
/// and there is no journal row in that table because nobody here has
/// hashed one (`docs/journal.md` §3, `machine/document.cpp`). What this
/// reader is really gated on is stronger and is answered where it can be:
/// the host has text for the entry, or it has not, and the reader says
/// which. The day an edition is added, `document_kind::journal` is one
/// field here — and the seam will already have been refusing to open
/// anything for the players it would then start refusing to arm for.
constexpr seam_definition journal_definition{
    .id = "journal",
    .about =
        "what the game cites, on the game's own screen; a Notes command "
        "on the party's own bar, or F1, for any entry, tale or "
        "proclamation",
    .fingerprints = journal_binaries,
    .points = journal_points,
    .trigger = false,
    .gate = document_kind::none,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& journal_seam() noexcept { return journal_definition; }

}  // namespace amberfolio::machine
