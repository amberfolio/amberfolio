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
// sentence: the word a numbered section of the document is called by —
// entry, tale, proclamation, each with its plural — and a number after it
// in the notation that section is numbered in (`journal_citations_in()`,
// journal.h).
//
// **Every word of the program's narration goes through one routine**, and
// it is not the one that draws a string at a cell. The message panel is
// drawn by a word-wrapping *box*: one far pointer to a Pascal string, a
// flag saying whether to home the cursor and clear the box first, a
// colour, and the box's four cells. The script's every PRINT ends there,
// the number form and the string form alike, so watching it is watching
// the narration itself rather than a routine that happens to be nearby.
//
// That is #232's finding, and it cost a driven run to learn: the watch
// used to be on the string drawer — a column, a row, a colour and a far
// Pascal string, the routine the Encamp Fix calls to write its report —
// and on the real program that routine draws the credits, the menus and
// the position line at the top of the viewport, and **not one word of the
// story**. A tour of the city that ends at the city hall with four
// proclamations named in one sentence produced no citation at all, and
// the reason was the address rather than the pattern.
//
// **A citation may arrive in two pieces**, and the box says so itself.
// The script prints a sentence as one operand and the number it cites as
// the next, appended with no space at all, so the box is called twice:
// once with the flag set, which is the message beginning, and once
// without, which is the rest of it. The watch keeps a rolling window of
// what has been drawn and empties it when the flag says a new message has
// begun (journal.h), which is the program's own message boundary rather
// than a guess about one.
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
// **The list is the exception, and takes every key there is.** It is the
// one thing this seam draws that covers the program's own screen, and the
// program's own command bar goes on running underneath it — so a key it
// left alone chose a command, or walked the party, on a screen nobody
// could see, and the program then painted its own bar and status line back
// over the journal to prove it. Nothing reaches the program while the list
// is up; `E` and Escape are the way out, and `E` because that is the word
// the screen puts on its bottom row and the letter of a word on a bar is
// how this game leaves every screen it has.
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
/// image. Five of them are `seam_automap.cpp`'s, so those have been
/// reached on a driven run of the real program since before this file
/// existed; the sixth is the message box, and #232 is the run that
/// reached it.
constexpr std::uint32_t key_pending_entry = 0xA6FD;
constexpr std::uint32_t key_read_entry = 0xA70F;
constexpr std::uint32_t clear_region_entry = 0x4047;
constexpr std::uint32_t clear_screen_entry = 0x7D3B;
constexpr std::uint32_t roster_drawn_return = 0x148A;
constexpr std::uint32_t message_box_entry = 0x77F8;

/// The string drawer, which this file **calls** and does not watch: the
/// Encamp Fix's call target, used here to put a page of somebody's
/// journal on the screen in the program's own font.
constexpr std::uint32_t draw_string_entry = 0x076B6;

/// The message box's frame at its entry, in the same convention: the far
/// return address on top, then the arguments with the first of them
/// nearest SP — the string, then the flag that says to home the cursor
/// and clear the box, then the colour, then the box's own four cells.
/// Only the first three are wanted here; the colour and the cells were
/// read on the run that found this address and are what identified it as
/// the message panel's own box, which is why they are named and not taken.
constexpr std::uint16_t box_frame_string_offset = 4;
constexpr std::uint16_t box_frame_string_segment = 6;
constexpr std::uint16_t box_frame_clear = 8;

/// The longest Pascal string there can be, which is what the box takes.
constexpr std::size_t longest_message = 255;

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

// --- The journal's own screen (M5-E4b, #222) -------------------------------
//
// **Drawn by the program, not by this seam.** The panel the reader uses is
// plane surgery because there is no routine that draws twelve rows of text
// in a box the size of the party roster. A full screen is different: the
// game has a bordered-window drawer that every Gold Box screen is made of,
// and a string drawer, and calling those two is how this screen gets the
// game's own border art, the game's own colours and the game's own
// lettering without this file knowing what any of them look like.
//
// **And it is given back by the program too.** The one thing a full-screen
// panel needs that the roster-sized one does not is a way to restore
// everything it covered, and there is exactly one: the routine the program
// itself calls to compose the adventuring screen. It takes no arguments
// and repaints the viewport, the status line and the roster.
//
// M5-E2d is why that is safe *here* and was not before. Closing a panel
// through the program's screen composer painted the 3D view over a vendor
// the player was talking to; this screen is only ever opened from the
// party's own command bar (#221), which is the one place in the game where
// a vendor cannot be on the screen.

/// The program's bordered-window drawer and its string drawer - the two
/// routines this screen is made of, and the same two the Encamp Fix's
/// report is made of (#188, `docs/seams.md` §3). Flat offsets in the
/// resident image.
constexpr std::uint16_t image_draw_frame = 0x041F8;

/// The program's own way of leaving a full-screen view: the per-mode screen
/// composer. It draws the scaffold — the outer frame, the bottom panel, the
/// viewport box and its inset — and then, for whichever mode the program is
/// in, the view, the party roster and the status line. It takes nothing and
/// cleans nothing, and it is reached as paragraph plus offset rather than as
/// a flat image offset, because it reaches its own literals as `CS:<constant>`
/// - the CS-relative hazard the automap records.
///
/// **Not the routine that *enters* the adventuring screen** (`0x2B5E`), which
/// is what this seam called first and what #175's teardown got wrong. That
/// one sets the mode byte to the alternate adventuring screen whether or not
/// the player was on it, and it draws the bottom panel alone - so the outer
/// frame, the viewport box and its ornaments never came back, and whatever
/// this screen had drawn above the panel stayed on the glass. This one is
/// the routine the program itself calls on the way out of every full-screen
/// view it has, and it repaints all of them.
constexpr std::uint16_t screen_redraw_offset = 0x27D9;

/// The screen, in the character cells the frame drawer counts in: the
/// whole of it but the bottom row, which is where the game keeps its
/// command bars and where this one puts its way out.
///
/// The top is row one and not row zero. The frame puts its title on the
/// box top row itself rather than on the border, so a box that started at
/// zero would have its border above the screen and its title clipped by
/// the edge - which is exactly what the first driven attempt looked like.
constexpr std::uint16_t list_frame_left = 0;
constexpr std::uint16_t list_frame_top = 1;
constexpr std::uint16_t list_frame_right = 0x27;
constexpr std::uint16_t list_frame_bottom = 0x17;
constexpr std::uint16_t list_frame_style = 0;

/// The colours: the game's own bright for a title and a highlighted line,
/// its own green for the rest. The same pair the reader's panel uses, so
/// the two halves of this enhancement look like one thing.
constexpr std::uint16_t list_title_colour = 0x0F;
constexpr std::uint16_t list_row_colour = 0x0A;

/// Where the rows go. The frame puts its title on the box's first interior
/// row, so the list starts below it.
constexpr std::uint16_t list_first_row = 3;
/// Ten, and the number is the batch's. A batch queues twelve calls
/// (`seam.h`), the frame takes one and the way out takes one, so ten
/// rows is exactly what is left - and the cursor scrolls the window
/// over a log that holds far more.
constexpr unsigned list_rows_visible = 10;

/// How many of them one pass paints. Five rows of forty characters is
/// under both of a batch's budgets with the frame beside them.
constexpr std::size_t list_rows_per_pass = 5;
constexpr std::uint16_t list_name_column = 1;
constexpr std::uint16_t list_exit_row = 0x18;

/// Where the right-hand column starts: far enough over that the
/// longest caption and number cannot reach it.
constexpr std::uint16_t list_when_column = 0x19;

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

/// A line for the journal's own screen, as a Pascal string.
///
/// Wider than `label` because a full screen is wider than the panel, and
/// length-prefixed because that is what the program's own string drawer
/// takes. Every character in it is this file's own: the section's caption,
/// a number, and a date this project computed. Never a word of the
/// program's.
class list_line {
 public:
  void add(std::string_view what) noexcept {
    for (const char ch : what) {
      if (length_ + 1 < text_.size()) {
        text_[++length_] = static_cast<std::uint8_t>(ch);
      }
    }
  }

  /// A number, optionally padded with leading zeroes - which is what a
  /// clock wants and a section number does not.
  void add(unsigned value, unsigned width = 0) noexcept {
    std::array<char, 5> digits{};
    std::size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    } while (value != 0 && count < digits.size());
    while (count < width) {
      digits[count++] = '0';
    }
    while (count > 0) {
      if (length_ + 1 < text_.size()) {
        text_[++length_] = static_cast<std::uint8_t>(digits[--count]);
      } else {
        --count;
      }
    }
  }

  /// Spaces out to `column`, so a line can carry its own right-hand
  /// column instead of costing a second call. A batch may queue twelve
  /// (`seam.h`), and a list that spent two on every row would run out
  /// halfway down itself - which is exactly what it did before this.
  void pad_to(std::size_t column) noexcept {
    while (length_ < column && length_ + 1 < text_.size()) {
      text_[++length_] = ' ';
    }
  }

  [[nodiscard]] std::span<const std::uint8_t> bytes() noexcept {
    text_[0] = static_cast<std::uint8_t>(length_);
    return {text_.data(), length_ + 1};
  }

 private:
  std::array<std::uint8_t, 41> text_{};
  std::size_t length_{};
};

/// One line of the log, as the screen shows it.
[[nodiscard]] list_line list_row_text(const journal_seen_row& row) {
  list_line line;
  // The unread mark, and a space where it is not - so the words line up
  // whether or not a line has one. Which line the cursor is on is said in
  // colour instead, the way the program says it on its own menus.
  line.add(row.read ? "  " : "* ");
  line.add(reader_word(row.what.kind));
  line.add(" ");
  line.add(row.what.number);
  return line;
}

/// The moment it was cited, onto the end of the line it belongs to.
void list_row_when(const journal_seen_row& row, list_line& line) {
  line.add(row.month, 2);
  line.add("-");
  line.add(row.day, 2);
  line.add("  ");
  line.add(row.hour, 2);
  line.add(":");
  line.add(row.minute, 2);
}

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

/// Up and down the list: the numpad's own eight and two, and the cursor
/// pad's arrows at the scan codes the same keys send with NumLock off.
/// Both spellings, because a player has both keys and the program reads
/// whichever the BIOS gave it.
/// The keystroke that makes the menu-bar routine return without choosing
/// anything: the routine answers a space by ending, and the loop above it
/// answers a letter it does not recognise by going round again.
constexpr std::uint8_t key_space_scan = 0x39;
constexpr std::uint8_t key_space_ascii = 0x20;

constexpr std::uint8_t key_step_back_char = '8';
constexpr std::uint8_t key_step_forward_char = '2';
constexpr std::uint8_t key_step_back_scan = 0x48;
constexpr std::uint8_t key_step_forward_scan = 0x50;

/// What a key claimed at the program's own **blocking read** is answered
/// with.
///
/// The poll is where a key is meant to be taken: the program asks whether
/// one is waiting, this seam takes it, and the program is told no. But a
/// key that lands in the step between that question and its answer arrives
/// at the *read* instead - and a read is the program having already
/// committed to being handed one. Take it there and put nothing back and
/// the program goes to sleep inside the BIOS, where **no point of this
/// engine is reached at all**, and the next key the player types is handed
/// straight to it, unseen. That is what made every second keystroke fall
/// through this seam's claim, and it is why "a seam-claimed key sometimes
/// needs a second press" was ever a thing anybody noticed.
///
/// So the read is answered, with a character the program throws away.
/// `-` is on none of its bars - their command letters are `0-9A-Z` - it is
/// neither of the two that step a bar's highlight, and it is not one of
/// the keypad characters the input routine remaps. Its menu-bar routine
/// therefore does not even return: it goes back to waiting, which is
/// exactly where it was.
constexpr std::uint8_t key_ignored_scan = 0x0C;
constexpr std::uint8_t key_ignored_ascii = '-';

/// The letter the list's own way out is named after, in both the cases a
/// player's keyboard sends. The screen says `EXIT`, and the letter of a
/// word on a bar is how every way out of every screen in this game is
/// taken - so it has to be one here too, and a player who reads the screen
/// must not have to guess at Escape.
constexpr std::uint8_t key_exit_upper = 'E';
constexpr std::uint8_t key_exit_lower = 'e';
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
  /// A step up or down the list (M5-E4b, #222). The numpad keys the game
  /// moves the party with, taken only while the list is the thing on the
  /// screen - the same modal claim the reader's other keys make.
  step_back,
  step_forward,
  /// Anything else, while the list has the whole screen: taken and
  /// dropped.
  ///
  /// The list is the only thing this seam draws that covers the program's
  /// own screen, and the program's own command bar goes on being live
  /// underneath it - the menu-bar routine is sitting in its key loop the
  /// whole time. Every key this seam did not want therefore *acted*, on a
  /// screen the player could not see: a letter picked a command off the
  /// bar the list was drawn over, and an arrow walked the party. Worse, it
  /// showed: the loop repaints its status line every time round and the
  /// bar routine repaints its bar, so the program drew its own screen back
  /// over the list a piece at a time and left something that looked like a
  /// corrupted game rather than a journal.
  ///
  /// So while the list is up, no keystroke reaches the program at all.
  /// That is a wider claim than any other this seam makes, and it is the
  /// one screen that has earned it: it is opened deliberately from the
  /// party's own bar, it covers everything, and it has its own way out.
  /// The panel modes make no such claim, and the file's header says why.
  swallow,
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
  if (mode == journal_reader_mode::listing) {
    if (key == key_return) {
      return claimable::accept;
    }
    // The keys the game itself moves the party with, on the numpad and on
    // the cursor pad, taken only while the list is up. A player who is
    // looking at a list expects up and down to move in it.
    const auto character = static_cast<std::uint8_t>(key & 0xFFU);
    const auto scan = static_cast<std::uint8_t>(key >> 8U);
    if (character == key_step_back_char ||
        (character == 0 && scan == key_step_back_scan)) {
      return claimable::step_back;
    }
    if (character == key_step_forward_char ||
        (character == 0 && scan == key_step_forward_scan)) {
      return claimable::step_forward;
    }
    if (character == key_exit_upper || character == key_exit_lower) {
      return claimable::close;
    }
    // And nothing else gets past. See `claimable::swallow`.
    return claimable::swallow;
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

/// Draw one string with the program's own string drawer.
[[nodiscard]] bool draw_line(seam_context& ctx, std::uint16_t image,
                             list_line& line, std::uint16_t colour,
                             std::uint16_t row, std::uint16_t column) {
  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  if (!ctx.place_bytes(line.bytes(), segment, offset)) {
    return false;
  }
  const std::array<std::uint16_t, 5> where{column, row, colour, segment,
                                           offset};
  return ctx.call_program(image, draw_string_entry, where);
}

/// One pass of the journal's own screen. True when the screen is finished.
///
/// **Painted over several arrivals**, because one batch cannot hold it: a
/// batch queues twelve calls and places 256 bytes (`seam.h`), and a frame,
/// ten rows of forty characters and a way out are more than either. So a
/// pass draws the frame if it has not been drawn, then as many rows as
/// fit, and says whether there is more to do. The program is sitting in
/// its own key loop while this happens and draws nothing itself, so a
/// screen that arrives in two pieces arrives in two pieces of one frame.
[[nodiscard]] bool draw_the_list(machine& box, seam_context& ctx) {
  journal_state& state = box.journal();
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::span<const journal_seen_row> rows = state.seen();
  std::size_t done = state.list_drawn();

  if (done == 0) {
    list_line title;
    title.add("ADVENTURER'S JOURNAL");
    std::uint16_t title_segment = 0;
    std::uint16_t title_offset = 0;
    if (!ctx.place_bytes(title.bytes(), title_segment, title_offset)) {
      return false;
    }
    const std::array<std::uint16_t, 8> frame{
        list_frame_left,   list_frame_top,   list_frame_right,
        list_frame_bottom, list_frame_style, list_title_colour,
        title_segment,     title_offset};
    if (!ctx.call_program(image, image_draw_frame, frame)) {
      return false;
    }
    if (rows.empty()) {
      list_line nothing;
      nothing.add("THE GAME HAS NOT SENT YOU HERE YET.");
      static_cast<void>(draw_line(ctx, image, nothing, list_row_colour,
                                  list_first_row + 1, list_name_column));
      list_line exit;
      exit.add("EXIT");
      return draw_line(ctx, image, exit, list_title_colour, list_exit_row,
                       list_name_column);
    }
  }

  // A window over the log, scrolled to keep the cursor in it. The log
  // holds far more than the screen shows, which is what the cursor is for.
  const std::size_t cursor = state.list_cursor();
  std::size_t top = 0;
  if (cursor >= list_rows_visible) {
    top = cursor - list_rows_visible + 1;
  }
  const std::size_t shown = rows.size() - top < list_rows_visible
                                ? rows.size() - top
                                : list_rows_visible;

  for (std::size_t drawn = 0; drawn < list_rows_per_pass && done < shown;
       ++drawn, ++done) {
    const journal_seen_row& row = rows[top + done];
    const std::uint16_t colour =
        top + done == cursor ? list_title_colour : list_row_colour;
    const auto at_row = static_cast<std::uint16_t>(list_first_row + done);
    list_line line = list_row_text(row);
    line.pad_to(list_when_column - list_name_column);
    list_row_when(row, line);
    if (!draw_line(ctx, image, line, colour, at_row, list_name_column)) {
      break;  // the batch is full; the next arrival carries on from here
    }
  }
  state.set_list_drawn(done);
  if (done < shown) {
    return false;
  }

  list_line exit;
  exit.add("EXIT");
  return draw_line(ctx, image, exit, list_title_colour, list_exit_row,
                   list_name_column);
}

/// Put the whole screen back, through the routine the program itself
/// leaves a full-screen view by.
///
/// The counterpart of `give_the_roster_back()` for a panel that took more
/// than the roster. Safe here for the reason the file's own header gives:
/// this screen is only opened from the party's own command bar, so there
/// is no vendor under it to paint over.
///
/// It repaints for whatever mode the program is in rather than putting it
/// on one, and it starts from the scaffold - so every cell this screen
/// covered is drawn again, which is what makes it a teardown rather than a
/// partial one.
void give_the_screen_back(machine& box, seam_context& ctx) {
  journal_state& state = box.journal();
  state.set_on_screen(false);
  state.set_drawn_signature(0);
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::array<std::uint16_t, 0> nothing{};
  static_cast<void>(ctx.call_program(
      static_cast<std::uint16_t>(image + roster_draw_paragraph),
      screen_redraw_offset, nothing));

  // **And one keystroke, so the program redraws its own command bar.**
  // Composing the screen is everything but the bar: the bar belongs to the
  // menu-bar routine, which is sitting in its key loop while all of this
  // happens and will not draw again until it returns. Without this the
  // screen comes back correctly with this seam's own `EXIT` still on the
  // bottom row, which is what the first driven attempt looked like.
  //
  // A space, because the routine answers a space by returning and the
  // adventuring loop answers a letter it does not know by going round
  // again - so the whole visible effect is the bar being drawn.
  static_cast<void>(ctx.inject_keystroke(key_space_scan, key_space_ascii));
}

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
  // What has to be given back depends on what was taken: the list took the
  // whole screen and the panel took the roster's cells, and asking the
  // program to repaint more than was covered is the M5-E2d bug.
  const bool took_the_screen = state.reader() == journal_reader_mode::listing;
  state.set_reader(journal_reader_mode::closed);
  state.clear_digits();
  if (!was_up) {
    return;
  }
  if (took_the_screen) {
    state.set_list_drawn(0);
    give_the_screen_back(box, ctx);
  } else {
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
      // **F1 still opens the prompt**, which is what it has always done.
      // The list has its own way in - the `Notes` command on the party's
      // own bar (#221) - and the two are different questions: "what was I
      // told?" is the list, "let me look something up" is this. A key that
      // changed what it did would have been a key a player had to relearn
      // for no reason.
      state.set_reader(journal_reader_mode::asking);
      return;
    case journal_reader_mode::listing:
      // On from the list to the prompt, which is how a player reaches the
      // ninety-odd entries nothing has cited yet without leaving the
      // journal to do it.
      give_the_screen_back(box, ctx);
      state.set_list_drawn(0);
      state.clear_digits();
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
///
/// `claimed` says whether a keystroke was taken off the buffer at all,
/// which the poll point does not care about and the blocking read must
/// (`key_ignored_ascii`).
[[nodiscard]] bool handle_keys(machine& box, seam_context& ctx,
                               std::uint16_t ds, bool& claimed) {
  journal_state& state = box.journal();
  std::uint16_t key = 0;
  const claimable which = claim_key(box.processor(), ds, state.reader(), key);
  claimed = which != claimable::none;
  switch (which) {
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
    case claimable::step_back:
      state.move_list_cursor(-1);
      return false;
    case claimable::step_forward:
      state.move_list_cursor(1);
      return false;
    case claimable::swallow:
      // Taken off the buffer and dropped. Nothing on the screen changes,
      // so nothing is drawn: the signature the next arrival computes is
      // the one already on the glass.
      return false;
    case claimable::accept:
      // Return on the list opens the line it is pointing at. The screen
      // goes back first, because what comes up is the reader's panel and
      // the panel lives on the adventuring screen.
      if (state.reader() == journal_reader_mode::listing) {
        const std::span<const journal_seen_row> rows = state.seen();
        if (rows.empty()) {
          return false;
        }
        const journal_citation wanted = rows[state.list_cursor()].what;
        give_the_screen_back(box, ctx);
        state.set_list_drawn(0);
        request(box, ctx, wanted);
        state.set_reader(journal_reader_mode::showing);
        state.set_page(0);
        return false;
      }
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
  // The list is drawn from the log and the cursor, so both are in the
  // signature: a line arriving at the top while the screen is up is a
  // screen that has to be drawn again.
  mix(static_cast<std::uint32_t>(state.seen().size()));
  mix(static_cast<std::uint32_t>(state.list_cursor()));
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

  // The list is not this seam's pixels at all: the program draws it, out
  // of the same two routines every Gold Box screen is made of, so there
  // is no buffer to rasterize and no font to read (#222).
  if (state.reader() == journal_reader_mode::listing) {
    // A pass at a time. Until the last one the signature is left alone, so
    // the next arrival comes back here and carries on rather than deciding
    // the screen is already right.
    if (draw_the_list(box, ctx)) {
      state.set_on_screen(true);
      state.set_drawn_signature(drawn);
    }
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
  bool claimed = false;
  if (handle_keys(box, ctx, ds, claimed)) {
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
///
/// **And a key taken here is answered**, because the program is already
/// committed to being handed one. `key_ignored_ascii` is the whole of
/// that argument; without it this point takes one key and the program
/// sleeps through the next.
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
  bool claimed = false;
  const bool batched = handle_keys(box, ctx, ds, claimed);
  if (claimed) {
    static_cast<void>(
        ctx.inject_keystroke(key_ignored_scan, key_ignored_ascii));
  }
  if (batched) {
    return;
  }
  draw_if_wanted(box, ctx, ds);
}

/// The program is about to put a message in its message box. Is it citing
/// an entry?
void at_message_box(machine& box, seam_context& ctx) {
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
      .offset = cpu.read_word(ss, at(sp, box_frame_string_offset)),
      .segment = cpu.read_word(ss, at(sp, box_frame_string_segment))};
  if (!followable(where, 1)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  journal_state& state = box.journal();

  // The program's own message boundary, taken rather than guessed: the
  // flag the box is asked to home and clear itself with is the script's
  // "this is a new message", and everything after it without the flag is
  // the same message continuing. Emptying the window here is what stops
  // a number appended to one sentence from being read against the one
  // before it.
  if (cpu.read_word(ss, at(sp, box_frame_clear)) != 0) {
    state.forget_citation();
  }

  const std::uint8_t length = cpu.read_byte(where.segment, where.offset);
  if (length == 0) {
    return;
  }
  const auto take =
      static_cast<std::uint8_t>(std::min<std::size_t>(length, longest_message));
  if (!followable(where, static_cast<std::uint32_t>(take) + 1U)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  std::array<char, longest_message> drawn{};
  for (std::uint8_t i = 0; i < take; ++i) {
    drawn[i] = static_cast<char>(cpu.read_byte(
        where.segment, at(where.offset, static_cast<std::uint16_t>(i + 1))));
  }

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
  // Every one the drawing named, not only the first: the city hall
  // names four proclamations in one sentence (#232), and a player who
  // reads the first wants the other three on the list with their `*`.
  // Last-named first, because the log puts each new line on top and the
  // list should read in the order the game said them.
  const wall_time when = box.wall().at(box.time());
  const std::span<const journal_citation> all = state.cited_all();
  for (std::size_t i = all.size(); i > 0; --i) {
    state.note_seen(all[i - 1], when.month, when.day, when.hour, when.minute);
  }
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
  state.set_reader(journal_reader_mode::listing);
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
      .offset = message_box_entry,
      .run = &at_message_box},
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
