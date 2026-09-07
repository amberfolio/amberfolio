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
// Where a page goes, and what decides it (M5-E4d, #305)
// -----------------------------------------------------
//
// **A page is drawn in two sizes**, and which one it gets is a fact about
// the machine rather than a memory of which key opened it.
//
// The small one is the automap's rect, from `automap.h`, which derives it
// once: the interior of the adventuring screen's right-hand frame less
// the program's own status row — 176 by 112 pixels, twenty-two columns of
// the program's eight-pixel font by fourteen rows. It is the honest size
// everywhere, because it is **the one region of this program's screen a
// seam can take and give back unconditionally**. The panel's cells are
// the party roster's, and the program can be asked to paint the roster
// again from live state (`give_the_roster_back()`); nothing else on the
// adventuring screen has that property, and a wider reader that covered
// something it could not restore is the M5-E2d bug — closing a panel
// through the program's screen composer painted the 3D view over the
// vendor the player was talking to.
//
// The big one is the whole screen, in the box the journal's own listing
// is drawn in: thirty-eight columns by twenty rows, 760 characters
// against 264. It needs the screen composer to put back what it covered,
// and the composer is safe on **one** precondition — that the party's own
// command-bar routine is the thing running, since that is the one place
// in the game where a vendor cannot be on the screen. `journal_state`'s
// `bar_live()` is that precondition, set and cleared at the two points
// this seam already has at that routine's call sites.
//
// So: a row of the listing, and the F1 prompt while the bar is live, open
// a full screen. F1 anywhere else with a roster — the camp screen, whose
// menu is a different call site, or an adventuring screen with a vendor's
// bar up — opens the panel. **A citation opens the panel, always**: it
// fires inside a script's own narration, where a vendor or an event's NPC
// can be in the viewport, and until that give-back has been measured a
// full screen there is M5-E2d again.
//
// **All three sizes of this seam's drawing are the same pixels** — the
// panel, the listing and a full-screen page — so the reader is modal over
// the automap: while it is open the map does not draw (one condition in
// `seam_automap.cpp`), and the map is drawn again when the entry is put
// away. That last part is one call and not a coincidence (M5-E4g, #332):
// a give-back paints through the program's own routines, and a batch of
// calls into the program is offered no points at all, so the two points
// the automap watches its cells with cannot see it happen. Both
// give-backs below tell it (`automap_state::note_panel_painted_over()`),
// and that is the whole of what the two seams say to each other; either
// still works with the other switched off.
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
// Space and Return are deliberately *not* taken while a **panel** page is
// up — a citation opens the reader in the middle of a story event, and
// the key that turns the game's own page has to stay the game's.
//
// **Anything that covers the whole screen takes every key there is**, and
// that is the listing and a full-screen page (#305). The program's own
// command bar goes on running underneath either — for a page that is the
// very reason it may be full-screen — so a key this seam left alone chose
// a command, or walked the party, on a screen nobody could see, and the
// program then painted its own bar and status line back over the journal
// to prove it. Nothing reaches the program while one is up.
//
// **So a full screen has a bar of its own** (M5-E4f, #317), and it is the
// same bar on both of them: `NEXT`, `PREV`, `EXIT`, on row `0x18`, which
// is the screen's last and the row this game draws every bar it has on.
// Three words and their first letters, because that is the only way this
// program is driven — `EXIT`, `LOOK`, `ENCAMP`, `AREA` — and because the
// two commands this enhancement had already added were spliced onto the
// program's own bars in order to look like the rest of them. It said
// `F1 MORE` and `ESC CLOSES` before, which names two keys this program
// has never asked anybody to press. `PREV` is genuinely new: F1 walked
// forward and closed on the last page, so there was no way back.
//
// **And it is laid out and painted the way the program's own bars are**
// (#329, #330), which took two goes and a person looking at it. It was
// indented by one and spaced by three, where every bar this game draws
// starts at column zero and puts one space between its commands; and it
// was drawn in one call in the bright, where every bar this game draws
// paints the **initial white and the tail green** - which is how a player
// is told which key picks which command, and why the program's own bars
// are stored mixed case, `Look`, `Encamp`, `Search`. A seam cannot borrow
// that rule, because it lives in the program's drawer and one call is one
// colour, so the bar is four calls now: the whole row in the green, then
// the three initials over it in the bright (`draw_the_bar()`).
//
// **The panel keeps `F1 MORE`**, and the difference is not laziness. A
// panel is drawn beside the program's own live command bar, so `N`, `P`
// and `E` are that bar's letters and taking them would pick the program's
// own commands out from under a player who can still see them; and
// twenty-two columns have no room for three words beside a `1/3` anyway.
// Only a screen that *covers* that bar can spell its keys as words. F1
// there still turns the page and closes on the last one, which its
// footer says.
//
// Escape closes from anywhere, whatever the bar says, because it costs
// nothing and somebody will press it. A page opened from a row of the
// listing goes back to the listing rather than out, so a person reading
// several entries stays in the journal.
//
// **The listing is twenty rows and pages rather than scrolls** (M5-E4e,
// #318 and #319). It filled ten rows of a twenty-row box on a reason that
// belonged to a version of it that painted in one batch; and it slid its
// window one row at a time, which is the one thing on this screen that
// could not have been in a 1988 program — every long list this game draws
// itself is replaced, never scrolled. So the screenful on the screen is
// the cursor's own page, `NEXT` and `PREV` replace it whole, and nothing
// slides. The cursor stays, because it is what `Return` opens.
//
//
// The entries that are pictures (#328)
// ------------------------------------
//
// Several of a journal's entries are drawings, and an OCR engine reads
// every word on such a page — which is the heading and a one-line
// caption. So the reader showed two lines and nineteen blank rows.
//
// A picture is reduced once, at ingestion, on the player's own machine
// (`hosts/common/.../journal_picture.h`), and kept in their own store; it
// crosses into this seam through a host service of its own,
// `journal_art`, into a buffer in `journal.h` on the same terms the
// entry's text crosses on. What arrives is **levels and not colours** —
// four tones — because tone is the whole of what one hue of ink on paper
// carries, and because the palette this program has installed is a fact
// about a running machine that an ingestion cannot know. `art_ramp`
// below is where a level becomes an index, and it is the knob.
//
// **A picture is a page of the entry**, after its text pages, in printed
// order: the caption is the text and the drawing follows it on the
// printed page, so `NEXT` walks from one into the other and `PREV` walks
// back. No new key, no new mode, and the paging #319 built already says
// which page a reader is on.
//
// It is drawn twice over, because a page is drawn in two sizes. The
// panel's copy is rasterized into this seam's own buffer at half scale
// and goes on the planes through the blit every other panel uses. The
// full screen has no such buffer — the program draws that box — so a
// picture there is plane surgery straight into the box's interior, in
// the arrival *after* the one that had the program draw the frame,
// because a handler's own writes land before a batch does.
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
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
//   * **Of the three citation shapes this watch answers, one is a
//     measured sentence and two are the pattern's own word** (#270).
//     What has been opened at a real citation is the city hall's four
//     proclamations — a Roman numeral after the section's own word. The
//     **entry** and **tale** forms have never been read off the program,
//     and that is exactly the state #232 caught wrong once already: the
//     shape wanted a decimal number where the game writes numerals, and
//     it took a driven run rather than a test to find out.
//     `docs/journal.md` §7 carries the same sentence.
//   * **The rows the test plan's phases 1, 3 and 4 left owed** are #270
//     as well, each named there by its matrix ID: NOT-9, because the
//     give-back is checked in one of the three screen modes; RDR-11's
//     two-store comparison; RDR-12 and NOT-11's cross-host `cmp`; CIT-5,
//     which wants a citation that is not on an event's last page so that
//     there is a page left to turn; and CIT-6, where it is the plan that
//     wants correcting rather than a run.
//   * **The half of this on the other side of `docs/journal.md` §9's
//     door has never been run by a person** (#236, on the list #274 keeps). The text this
//     reader draws is handed to it by a host, and the two paths that
//     produce it for a player — the dev page's journal panel in a real
//     browser, and the desktop's shipping OCR path, which runs the
//     player's *own* installed engine as a program — are exercised by
//     nothing but a fixture engine in CI. This seam cannot be wrong
//     about them, and cannot vouch for them either.

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
#include "seam_key_read.h"

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

/// How many pages of one entry the reader will count to.
///
/// **Not derived from a full page**, which is why it did not move when a
/// page grew from 264 characters to 760 (#305). What it bounds is the
/// *walk* over a four-kilobyte buffer, and the fewest characters a page
/// can hold under either shape is one to a line — 12 in the panel and 20
/// on the screen — so the walk's real worst case is 341 pages and 205,
/// and neither is a number a reader would ever count to. Sixty-four is
/// past any entry a printed journal holds, in either size, and stops a
/// pathological buffer from being walked for ever.
constexpr unsigned reader_max_pages = 64;

/// The colours, which are the program's own: the title in the yellow it
/// highlights with, the body in the green it writes messages in, and the
/// footer in grey so it reads as a label rather than as more of the text.
constexpr std::uint8_t colour_black = 0;
constexpr std::uint8_t colour_footer = 7;
constexpr std::uint8_t colour_body = 10;
constexpr std::uint8_t colour_title = 14;

/// **The ramp**: which palette index each of a stored picture's four
/// levels is drawn in (#328).
///
/// A picture crosses as *tone* and never as colour, for the reason
/// `machine/journal.h` gives at length: the drawings are one hue of ink
/// on paper, and the palette the program has installed is a fact about a
/// running machine that an ingestion cannot know. So the choice of index
/// is made here, where the rest of this panel's colours are chosen, and
/// **it is a knob**: changing it re-draws every player's pictures and
/// invalidates nobody's ingestion, which is the same arrangement
/// `explored_reveal_radius` has with the automap's sidecar.
///
/// Ink first, because level zero is the darkest sample and this panel
/// draws on a *black* ground — so the most ink becomes the brightest
/// index and the paper becomes the ground itself. Four greys of the
/// sixteen: the program's own bright, its grey, its dark grey, and
/// black.
///
/// **Nobody has looked at one of these on a display** (`docs/journal.md`
/// §11.2), and #263 and #299 are two recorded instances of exactly that
/// gap producing the wrong answer twice. This is the first thing to
/// change when somebody does.
constexpr std::array<std::uint8_t, journal_art_levels> art_ramp{15, 7, 8,
                                                                colour_black};
static_assert(art_ramp[journal_art_paper] == colour_black,
              "paper is the panel's own ground and is drawn as nothing");

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

/// The menu bar's highlight: **one byte in the data segment that every
/// bar in the program shares**, a one-based index of the group it is
/// sitting on (M5-E1g, #304, whose own copy of this fact is in
/// `seam_encamp_fix.cpp`; each seam carries its own fact table). The
/// routine sets it to the group of the command it matched, and the
/// current group is the one drawn white end to end - which is what
/// `Notes` was doing on the maintainer's frame (#330).
constexpr std::uint16_t data_bar_highlight = 0x6B2B;

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
// lettering without this file knowing what any of them look like. Since
// #305 a page of an entry is made of the same two, in the same box.
//
// **And it is given back by the program too.** The one thing a full-screen
// panel needs that the roster-sized one does not is a way to restore
// everything it covered, and there is exactly one: the routine the program
// itself calls to compose the adventuring screen. It takes no arguments
// and repaints the viewport, the status line and the roster.
//
// M5-E2d is why that is safe *here* and was not before. Closing a panel
// through the program's screen composer painted the 3D view over a vendor
// the player was talking to. The listing has one way in — the party's own
// command bar (#221) — and the general rule that way in is an instance of
// is `bar_live()`: the composer may be used exactly while the party's own
// menu-bar routine is the thing running.

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
/// interior, because **the drawer's border falls outside the rectangle
/// on all four sides**. A box of (left, top, right, bottom) puts its
/// horizontal runs on rows `top - 1` and `bottom + 1` and its two
/// vertical runs in columns `left - 1` and `right + 1`, and the cells
/// named are what is left for a title and for text.
///
/// So the widest a full screen can be is one in and one up from the
/// edges, which is the camp panel's own rectangle in
/// `seam_encamp_fix.cpp` and is where these numbers now come from. The
/// first version of this screen asked for the whole of it — column zero
/// to column `0x27`, and a bottom of `0x17` — and got both of the
/// defects a person looking at it reported:
///
///   * **The corners were busted.** A vertical run in column `-1` or
///     column `0x28` is not clipped: the video window is row-major and
///     eight pixels to a byte, so the byte before a scanline's first is
///     the *previous* scanline's last. The left border came out at the
///     right-hand edge one pixel row high and the right border at the
///     left-hand edge one pixel row low — which is why the two chains
///     were two pixel rows out of phase with each other, and why each
///     showed only one of its two end caps.
///   * **The way out sat on the border.** A bottom of `0x17` puts the
///     lower run on row `0x18`, and row `0x18` is the screen's last —
///     the row every bar in this game is drawn on and the row this
///     screen puts `EXIT` on. One row up is `0x16`, which leaves the
///     border on `0x17` and the whole of the bottom row to the way out.
///
/// The top is row one and not row zero, and that one is not new. The
/// frame puts its title on the box top row itself rather than on the
/// border, so a box that started at zero would have its border above
/// the screen and its title clipped by the edge - which is exactly what
/// the first driven attempt looked like.
constexpr std::uint16_t list_frame_left = 1;
constexpr std::uint16_t list_frame_top = 1;
constexpr std::uint16_t list_frame_right = 0x26;
constexpr std::uint16_t list_frame_bottom = 0x16;
constexpr std::uint16_t list_frame_style = 0;

/// The colours: the game's own bright for a title and a highlighted line,
/// its own green for the rest. The same pair the reader's panel uses, so
/// the two halves of this enhancement look like one thing.
constexpr std::uint16_t list_title_colour = 0x0F;
constexpr std::uint16_t list_row_colour = 0x0A;

/// Where the rows go. The frame puts its title on the box's first interior
/// row, so the list starts below it.
constexpr std::uint16_t list_first_row = 3;
/// How many of them there are: the box's whole body, every row of it
/// (M5-E4e, #318).
///
/// It was ten, and the reason given was a batch's - twelve calls
/// (`seam.h`), one for the frame and one for the way out, so ten is what
/// is left. That was true of the version that painted this screen in one
/// go and of no version since: `list_rows_per_pass` below is what one
/// batch draws, and `screen_drawn()` is how the next one carries on. So
/// the budget bounds a **pass** and the number of passes is free, which
/// leaves the *box* as the only thing bounding the screen - and the box's
/// body is twenty rows, the same twenty a full-screen page lays its text
/// into (`screen_page.rows`, below, whose expression this is). Ten of
/// them filled and ten empty was a fossil of the old reason.
constexpr unsigned list_rows_visible = list_frame_bottom - list_first_row + 1;
static_assert(list_rows_visible == 20,
              "the listing fills the box it is drawn in");

/// How many of them one pass paints. Five rows of forty characters is
/// under both of a batch's budgets with the frame beside them - four
/// passes for a full screen of rows where it used to be two, and the
/// program is sitting in its own key loop drawing nothing for all four.
///
/// The tightest pass this leaves is a log of five rows or fewer, where
/// the clear, the frame, five rows and the bar's four calls (#330) are
/// all one arrival: 11 of a batch's 12 calls and 253 of its 256 bytes.
/// **Going over is not a defect**, and that is why the number was left at
/// five: `draw_the_bar()` returns false, the pass returns false with
/// `screen_drawn()` already at the last row, and the next arrival draws
/// the bar and nothing else. A pass that does not fit costs one more
/// arrival of a program that is drawing nothing.
constexpr std::size_t list_rows_per_pass = 5;

/// How many screenfuls a log of `rows` lines comes to (M5-E4e, #319).
///
/// **At least one**, because an empty log is still a screen: it says so
/// in a line of its own, and a listing of no pages would have nothing to
/// draw that line on.
[[nodiscard]] constexpr std::size_t list_pages(std::size_t rows) noexcept {
  return rows == 0 ? 1U : ((rows + list_rows_visible - 1U) / list_rows_visible);
}
constexpr std::uint16_t list_name_column = 1;

/// The way out, on the screen's own last row - below the frame, where
/// this game draws every bar it has. It starts at column zero and spans
/// the row, because clearing the bar it covers is its second job.
constexpr std::uint16_t list_exit_row = 0x18;
constexpr std::uint16_t list_exit_column = 0;
constexpr std::size_t list_row_cells = 40;

/// The three words on it, in order, and the whole of its geometry
/// (M5-E4f, #317; #329).
///
/// The bar is built from this table rather than from a literal, so the
/// column each initial is drawn at is derived from the words in front of
/// it instead of counted by hand - `NEXT PREV EXIT`, flush from column
/// zero with **one** space between, which is what every bar this program
/// draws does.
constexpr std::array<std::string_view, 3> bar_words{"NEXT", "PREV", "EXIT"};

/// Its two colours (#330): the game's own green for the words and its own
/// bright for the letter each one is picked by. The pair every command bar
/// in this game is painted in, and the reason its own bars are stored
/// mixed case - the program's drawer colours by case, and a seam handing
/// that drawer a string of its own gets one colour a call.
constexpr std::uint16_t bar_word_colour = list_row_colour;
constexpr std::uint16_t bar_key_colour = list_title_colour;

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

/// How wide a page is, and how many lines of it there are.
///
/// **There are two shapes** (M5-E4d, #305), and everything below takes
/// one rather than reading a constant: the roster-sized panel the page
/// has always had, and the full screen the `Notes` listing takes. The
/// wrap does not change — it only gets wider.
struct page_shape {
  int columns;
  int rows;
};

/// The panel's, which is the geometry at the top of this file.
constexpr page_shape panel_page{.columns = reader_columns,
                                .rows = reader_body_rows};

/// The full screen's, **derived from the listing's frame rather than
/// restated**: the interior is what the frame drawer leaves, and the body
/// begins below the row the frame writes its title on. A page and the
/// listing are the same box with different things in it, so a number that
/// moved for one and not the other would be a defect nobody would see
/// until a person looked at the screen.
constexpr page_shape screen_page{
    .columns = list_frame_right - list_frame_left + 1,
    .rows = list_frame_bottom - list_first_row + 1};

static_assert(screen_page.columns == 38,
              "the screen's interior is thirty-eight glyphs wide");
static_assert(screen_page.rows == 20, "and twenty rows of it are the body");

/// **And the same box in pixels, which is what a picture is reduced to**
/// (#328). `journal_art_width`/`_height` are declared in `journal.h`
/// because a host reduces to them at ingestion, long before there is a
/// machine; they are held against the frame *here*, because this file is
/// the only thing that knows what the frame does. A host that reduced to
/// a shape this screen had no room for would produce a store nobody
/// could draw, and the store outlives the build that wrote it.
static_assert(journal_art_width ==
                  static_cast<unsigned>(screen_page.columns * glyph_columns),
              "a picture is as wide as the page's interior");
static_assert(journal_art_height ==
                  static_cast<unsigned>(screen_page.rows * glyph_rows),
              "and as deep");

/// The panel is **exactly half the page**, which is what lets one stored
/// picture serve both shapes: half of the widest page is 152x80 and the
/// panel's body is 176x96, so a picture reduced for the page always fits
/// the panel when it is halved, whatever its own proportions are.
static_assert(journal_art_width / 2U <= automap_panel_width,
              "half a page's picture fits the panel across");
static_assert(journal_art_height / 2U <=
                  static_cast<unsigned>(reader_body_rows * glyph_rows),
              "and down");

/// Where that box begins on the 320x200 screen, in pixels: the interior
/// of the frame, which starts one cell in and below the row the frame
/// writes its title on. The same two numbers the rows are drawn at, in
/// pixels rather than character cells, because a picture is not on the
/// character grid and everything else here is.
constexpr int art_screen_x = list_frame_left * glyph_columns;
constexpr int art_screen_y = list_first_row * glyph_rows;
static_assert(art_screen_x == 8 && art_screen_y == 24,
              "the page's interior begins at (8, 24)");

/// The most rows either shape asks for, which is what one laid-out page
/// is sized to.
constexpr int reader_max_body_rows = screen_page.rows;
static_assert(reader_max_body_rows >= panel_page.rows,
              "a laid-out page has to hold the taller of the two shapes");

/// The widest a laid-out row can be, which is the wider of the two shapes.
constexpr std::size_t page_line_max =
    static_cast<std::size_t>(screen_page.columns);

/// One laid-out row.
///
/// **Owned, rather than a view into the entry's own text** — which is
/// what it was until the reflow below (#316). A row is now assembled out
/// of pieces that are not side by side in the store: two of the scan's
/// own lines joined by a space, and the two halves of a word the
/// typesetter hyphenated across a line break joined by nothing at all.
/// Neither of those is a substring of anything, so neither can be a view
/// of one.
///
/// It converts to a `std::string_view` implicitly because every consumer
/// of a row wants one and not one of them cares where the bytes live.
struct page_line {
  std::array<char, page_line_max> ch{};
  unsigned length{};

  [[nodiscard]] std::size_t size() const noexcept { return length; }
  [[nodiscard]] bool empty() const noexcept { return length == 0; }
  void clear() noexcept { length = 0; }
  void add(char one) noexcept {
    if (length < ch.size()) {
      ch[length++] = one;
    }
  }
  void add(std::string_view more) noexcept {
    for (const char one : more) {
      add(one);
    }
  }
  [[nodiscard]] operator std::string_view() const noexcept {
    return {ch.data(), length};
  }
};

/// One page of wrapped text: up to the shape's rows of lines, where the
/// text after them begins, and whether there is any.
struct page_layout {
  std::array<page_line, reader_max_body_rows> line{};
  unsigned lines{};
  std::size_t next{};
  bool more{false};
};

[[nodiscard]] constexpr bool is_space(char ch) noexcept {
  return ch == ' ' || ch == '\t' || ch == '\r';
}

/// The most characters one word of the reflowed text runs to.
///
/// Twice the widest row, and both bounds on it are real. It has to be at
/// least a row wide or a word could never fill one; and it has to be
/// small enough that a word always fits on a page that *starts* empty,
/// because a word that can never be placed is a walk that stands still.
/// Twice thirty-eight is four rows of the panel's twenty-two and two of
/// the screen's thirty-eight, against pages twelve and twenty rows deep.
/// A run longer than this is cut here and what is left of it is the next
/// word — which is what the wrap already did at the right-hand edge.
constexpr std::size_t page_word_max = 2 * page_line_max;
static_assert(page_word_max <= static_cast<std::size_t>(panel_page.columns) *
                                   static_cast<std::size_t>(panel_page.rows),
              "a word has to fit on an empty page of the smaller shape");

/// What the stored text reads as once its line breaks are read the way an
/// OCR engine meant them (#316).
enum class token_kind : std::uint8_t {
  end,        ///< nothing but whitespace between here and the end
  paragraph,  ///< a blank line, which is the one break the wrap honours
  word,       ///< a run of characters, perhaps rejoined across a hyphen
};

struct token {
  token_kind kind{token_kind::end};
  std::size_t next{};  ///< where the scan for the token after it begins
  unsigned length{};   ///< how much of `into` a word filled
};

/// Whether a character is one a typesetter's break hyphen may sit between.
///
/// Checked on **both** sides before two fragments are joined, because the
/// join is a guess and this is the cheap half of narrowing it. What it
/// cannot tell apart is `WITH-` (a word broken across a column) from
/// `WELL-` (a compound that happened to break there); nothing short of a
/// dictionary can. What it does rule out is the dash on a line of its
/// own, and the `-` an engine reads off a rule or a fold — either of
/// which would otherwise swallow the word after it.
[[nodiscard]] constexpr bool hyphen_may_join(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9');
}

/// The next token at or after `from`, with a word's characters written
/// into `into`.
[[nodiscard]] token next_token(std::string_view text, std::size_t from,
                               std::span<char> into) noexcept {
  token out;

  // The whitespace first, counting the line breaks in it: **one** is the
  // end of a printed line and reads as a space, **two or more** is a
  // blank line and reads as a paragraph break.
  std::size_t p = from;
  unsigned breaks = 0;
  while (p < text.size() && (is_space(text[p]) || text[p] == '\n')) {
    breaks += static_cast<unsigned>(text[p] == '\n');
    ++p;
  }
  if (p >= text.size()) {
    out.next = text.size();
    return out;
  }
  if (breaks >= 2) {
    out.kind = token_kind::paragraph;
    out.next = p;
    return out;
  }

  out.kind = token_kind::word;
  std::size_t len = 0;
  for (;;) {
    while (p < text.size() && !is_space(text[p]) && text[p] != '\n' &&
           len < into.size()) {
      into[len++] = text[p++];
    }
    out.next = p;
    if (len >= into.size()) {
      break;  // cut at the buffer; the rest of the run is the next word
    }

    // Hyphenated across the scan's own line break? Only when the run ends
    // on a hyphen with a letter before it, exactly one newline follows,
    // and a letter follows that.
    if (len < 2 || into[len - 1] != '-' || !hyphen_may_join(into[len - 2])) {
      break;
    }
    std::size_t look = p;
    unsigned over = 0;
    while (look < text.size() && (is_space(text[look]) || text[look] == '\n')) {
      over += static_cast<unsigned>(text[look] == '\n');
      ++look;
    }
    if (over != 1 || look >= text.size() || !hyphen_may_join(text[look])) {
      break;
    }
    --len;  // the hyphen belonged to the printed column, not to the word
    p = look;
  }
  out.length = static_cast<unsigned>(len);
  return out;
}

/// Greedy word wrap from `start`, at most one page of it.
///
/// **The scan's line breaks are not the page's** (#316), and the comment
/// that used to stand here said the opposite: it read a single newline as
/// the end of a line. An OCR engine emits one newline per *printed* line,
/// so honouring one drew the sixty-odd character column of a player's own
/// journal into a page twenty-two or thirty-eight wide and threw a third
/// of every row away — and re-hyphenated `WITH-` / `IN` in the middle of
/// a row that had room for the whole word.
///
/// So the rule is the one an engine's output actually carries: a single
/// newline is a **space**; a blank line — two or more newlines in a row —
/// is a **paragraph break** and gets one blank row, however many blank
/// lines there were; and a line ending in a hyphen **joins** to the word
/// after it with the hyphen dropped.
///
/// The de-hyphenation is a guess, and it is made *here* rather than at
/// ingestion for that reason: the store keeps what the engine read, so a
/// player proof-reading their own transcription still sees the lines the
/// engine saw, a correction is written against those lines, and a better
/// rule later needs no re-ingestion. Both shapes get it at once, too,
/// because both come through this function. `next_token()` holds the
/// guess and `hyphen_may_join()` is what narrows it.
///
/// What has not changed: it breaks at spaces, and a word longer than the
/// shape is wide is broken where it runs out of columns, because a word
/// that cannot fit has to go somewhere and dropping it would be losing
/// the player's own text.
[[nodiscard]] page_layout lay_out(std::string_view text, std::size_t start,
                                  page_shape shape) {
  page_layout page;
  const auto rows = static_cast<unsigned>(shape.rows);
  const auto columns = static_cast<std::size_t>(shape.columns);
  std::array<char, page_word_max> word{};

  // Three positions, and the distance between them is what keeps a page
  // resumable now that a row is assembled rather than pointed at: `scan`
  // is where the reading is, `current` is a row being filled that no page
  // holds yet, and `committed` is the end of the last word that reached a
  // row — the only offset the *next* page may be started from.
  std::size_t scan = std::min(start, text.size());
  std::size_t committed = scan;
  std::size_t current_end = scan;
  page_line current;

  const auto flush = [&]() {
    page.line[page.lines++] = current;
    committed = current_end;
    current.clear();
  };

  while (page.lines < rows) {
    const token piece = next_token(text, scan, word);
    if (piece.kind == token_kind::end) {
      break;
    }

    if (piece.kind == token_kind::paragraph) {
      if (current.empty() && page.lines == 0) {
        // A page never opens on a blank row: the break between two pages
        // is already a break, and a row spent saying so is a row of the
        // player's own text not shown.
        scan = piece.next;
        committed = piece.next;
        current_end = piece.next;
        continue;
      }
      if (!current.empty()) {
        flush();
      }
      if (page.lines >= rows) {
        break;
      }
      page.line[page.lines++] = page_line{};
      committed = piece.next;
      current_end = piece.next;
      scan = piece.next;
      continue;
    }

    const std::string_view one{word.data(), piece.length};
    if (one.size() > columns) {
      // Longer than a row. It takes rows of its own, and it takes all of
      // them on one page: the offset a page resumes from can name the
      // start of a word but never the middle of one this file assembled
      // rather than found.
      if (!current.empty()) {
        flush();
      }
      const auto needs =
          static_cast<unsigned>((one.size() + columns - 1) / columns);
      if (page.lines + needs > rows) {
        break;  // it goes on the next page, which starts empty and holds it
      }
      for (std::size_t at = 0; at < one.size(); at += columns) {
        page_line row;
        row.add(one.substr(at, columns));
        page.line[page.lines++] = row;
      }
      committed = piece.next;
      current_end = piece.next;
      scan = piece.next;
      continue;
    }

    if (current.empty()) {
      current.add(one);
    } else if (current.size() + 1 + one.size() <= columns) {
      current.add(' ');
      current.add(one);
    } else {
      flush();
      if (page.lines >= rows) {
        break;  // the word is unplaced, and `committed` is behind it
      }
      current.add(one);
    }
    current_end = piece.next;
    scan = piece.next;
  }

  if (!current.empty() && page.lines < rows) {
    flush();
  }

  std::size_t p = committed;
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

[[nodiscard]] page_walk walk_pages(std::string_view text, unsigned wanted,
                                   page_shape shape) {
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
    const page_layout laid = lay_out(text, at_byte, shape);
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

/// How many pages an entry has, and where its pictures start (#328).
///
/// **A picture is a page of the entry, after its text pages, in printed
/// order.** The caption is the text and the drawing follows it on the
/// printed page, so `NEXT` walks from the caption into the picture and
/// `PREV` walks back — no new key, no new mode, and the paging #319 built
/// already says which page a reader is on.
///
/// A refusal is one page too. It has to be, because an entry can be a
/// drawing with pictures and *no* text — a picture is reduced whether or
/// not an OCR engine was installed, since a drawing has no words in it
/// (`docs/journal.md` §11.4) — and a reader that made the refusal the
/// whole entry would show `NOTHING WAS READ` over a picture it was
/// holding.
struct reader_pages {
  /// Pages of the entry's own text, or the one page a refusal is.
  unsigned text{1};
  /// Pictures after them, as the last callout said the entry has.
  unsigned art{0};

  [[nodiscard]] unsigned total() const noexcept { return text + art; }
};

[[nodiscard]] reader_pages count_pages(const journal_state& state,
                                       page_shape shape) {
  reader_pages pages;
  pages.text = state.delivery() == journal_delivery::ready
                   ? walk_pages(state.text(), 0, shape).count
                   : 1;
  pages.art = state.art_count();
  return pages;
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

/// Which picture a callout is about, as the two questions the reader
/// asks of the buffer (#328).
///
/// The pair as well as the number, because tale 4 and entry 4 are two
/// documents and both may have art: a reader that checked only which
/// picture would draw the tale's map on the entry's page for as long as
/// it took the callout to come back.
[[nodiscard]] bool art_names(const journal_state& state,
                             unsigned nth) noexcept {
  return state.art_of() == state.entry() && state.art_nth() == nth;
}

/// Whether the buffer is holding that picture — what the drawing asks.
[[nodiscard]] bool art_is_here(const journal_state& state,
                               unsigned nth) noexcept {
  return state.art_ready() && art_names(state, nth);
}

/// Whether a callout for it has already come back, with or without a
/// picture — what the *fetch* asks, and the difference matters: a host
/// that has the count and not the record would otherwise be asked again
/// on every arrival, for as long as the page was up.
[[nodiscard]] bool art_was_asked(const journal_state& state,
                                 unsigned nth) noexcept {
  return state.art_answered() && art_names(state, nth);
}

/// What the panel says instead of a picture when the host had the count
/// and not the picture — a store with a record missing, or one written by
/// a build that could not make one.
constexpr refusal art_refusal{.first = "THE PICTURE", .second = "IS NOT HERE"};

/// One of the entry's pictures into the panel, **halved**.
///
/// The stored picture is the full-screen page's box and the panel is
/// exactly half of it, which is what lets one stored picture serve both
/// shapes (`machine/journal.h`, `docs/journal.md` §11.2). So the panel's
/// copy is a 2x1 average of the levels rather than a second bitmap in a
/// player's store.
///
/// **Averaged and not sampled**, and rounded toward ink on a tie: these
/// drawings are hairlines, and a nearest reduction that landed between
/// two of them would drop the line entirely — which is the same reason
/// the reducer at ingestion is a box filter (`journal_picture.h`). A tie
/// goes to the darker level because losing a line is the failure that
/// cannot be seen and darkening one is the failure that can.
///
/// Paper is drawn, not skipped: `art_ramp` maps it to the panel's own
/// black, so the margin around a portrait picture is the ground the rest
/// of the panel is on.
void draw_art(panel_pixels& panel, const journal_state& state) noexcept {
  const unsigned width = (state.art_width() + 1U) / 2U;
  const unsigned height = (state.art_height() + 1U) / 2U;
  const int left = (panel_width - static_cast<int>(width)) / 2;
  const int top =
      reader_body_y +
      (((reader_body_rows * glyph_rows) - static_cast<int>(height)) / 2);
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x) {
      const unsigned sum = state.art_level_at(2U * x, 2U * y) +
                           state.art_level_at((2U * x) + 1U, 2U * y) +
                           state.art_level_at(2U * x, (2U * y) + 1U) +
                           state.art_level_at((2U * x) + 1U, (2U * y) + 1U);
      const auto level = static_cast<std::uint8_t>((sum + 1U) / 4U);
      put(panel, left + static_cast<int>(x), top + static_cast<int>(y),
          art_ramp[level]);
    }
  }
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

  // How many pages there are, text and pictures together (#328), and
  // which of them this is. The page is clamped rather than trusted: an
  // entry can lose pages under a page number that was right for the one
  // before it — a shorter text, or a host that answered the count and
  // then answered nothing.
  const reader_pages pages = count_pages(state, panel_page);
  const unsigned total = pages.total();
  const unsigned page = state.page() < total ? state.page() : total - 1U;
  const bool more = page + 1U < total;

  // The footer is the same three things on every page of an entry, so it
  // is built once here and drawn at the end of each of the three arms
  // below.
  const auto footer_line = [&](bool ends_short) {
    label footer;
    if (total > 1) {
      footer.add(page + 1U);
      footer.add("/");
      footer.add(total);
      footer.add("  ");
    }
    footer.add(more ? "F1 MORE" : "F1 CLOSES");
    if (ends_short) {
      // The entry was longer than the buffer that crossed the host
      // boundary (journal.h). Said rather than silently stopped: a
      // transcription with a hole in it that nothing mentions is the
      // failure a player finds out about last.
      footer.add(" +");
    }
    draw_centred(panel, reader_footer_y, footer.view(), colour_footer, font);
  };

  if (page >= pages.text) {
    // **A picture of the entry**, halved into the panel (#328).
    const unsigned nth = page - pages.text;
    if (art_is_here(state, nth)) {
      draw_art(panel, state);
    } else {
      draw_centred(panel, reader_body_y + (4 * glyph_rows), art_refusal.first,
                   colour_body, font);
      draw_centred(panel, reader_body_y + (5 * glyph_rows), art_refusal.second,
                   colour_body, font);
    }
    footer_line(false);
    return total;
  }

  if (state.delivery() != journal_delivery::ready) {
    const refusal what = refusal_for(state.delivery());
    draw_centred(panel, reader_body_y + (4 * glyph_rows), what.first,
                 colour_body, font);
    draw_centred(panel, reader_body_y + (5 * glyph_rows), what.second,
                 colour_body, font);
    if (total == 1) {
      // Nothing to page to, so the panel says the one thing there is to
      // do rather than a page counter and a key that turns nothing.
      label footer;
      footer.add("ESC CLOSES");
      draw_centred(panel, reader_footer_y, footer.view(), colour_footer, font);
      return 1;
    }
    footer_line(false);
    return total;
  }

  const std::string_view text = state.text();
  const page_walk walk = walk_pages(text, page, panel_page);
  const page_layout laid = lay_out(text, walk.start, panel_page);
  for (unsigned row = 0; row < laid.lines; ++row) {
    draw_text(panel, 0, reader_body_y + (static_cast<int>(row) * glyph_rows),
              laid.line[row], colour_body, font);
  }
  footer_line(!more && state.truncated());
  return total;
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

/// A picture onto the planes, whole, in the box the program has just
/// drawn (#328).
///
/// The panel's own picture goes through `render()` and the blit above,
/// because the panel is this seam's linear buffer and always was. A
/// full-screen page has no such buffer — the program draws that screen,
/// out of its own frame drawer and its own string drawer, and a
/// byte-per-pixel copy of its interior would be forty-eight kilobytes of
/// core for something that is on the glass for one page. So the packed
/// levels are walked **in place**: a byte of a plane is eight pixels
/// looked up through the ramp, and nothing the size of the box is
/// materialized.
///
/// **Only the picture's own byte columns are written**, and the rows
/// outside it are left alone: the caller has just had the program clear
/// this box, so the margin is already the ground. Pixels that fall
/// inside a byte the picture only partly covers are written as that same
/// ground, which is what makes a picture whose width is not a multiple of
/// eight land without a fringe.
///
/// It is called **after** the batch that drew the frame, and that
/// ordering is the whole of why it is a second pass: a handler's own
/// writes land the instant it runs and a call into the program lands
/// when the batch does, so a picture drawn beside the frame in one pass
/// would be painted over by the frame it was drawn beside. It is #303's
/// ordering, one screen up.
void blit_art(machine& box, const journal_state& state) {
  cpu::processor& cpu = box.processor();

  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);

  const auto width = static_cast<int>(state.art_width());
  const auto height = static_cast<int>(state.art_height());
  const int left =
      art_screen_x + ((static_cast<int>(journal_art_width) - width) / 2);
  const int top =
      art_screen_y + ((static_cast<int>(journal_art_height) - height) / 2);
  const int first_byte = left / 8;
  const int last_byte = (left + width - 1) / 8;

  for (std::uint8_t plane = 0; plane < ega::plane_count; ++plane) {
    write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                   sequencer_map_mask_index,
                   static_cast<std::uint8_t>(1U << plane));
    for (int row = 0; row < height; ++row) {
      const auto line =
          static_cast<std::uint16_t>((top + row) * plane_bytes_per_row);
      for (int column = first_byte; column <= last_byte; ++column) {
        std::uint8_t bits = 0;
        for (int bit = 0; bit < 8; ++bit) {
          const int x = ((column * 8) + bit) - left;
          const std::uint8_t colour =
              x >= 0 && x < width
                  ? art_ramp[state.art_level_at(static_cast<unsigned>(x),
                                                static_cast<unsigned>(row))]
                  : colour_black;
          if (((colour >> plane) & 1U) != 0) {
            bits = static_cast<std::uint8_t>(bits | (0x80U >> bit));
          }
        }
        cpu.write_byte(video_window_segment,
                       at(line, static_cast<std::uint16_t>(column)), bits);
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
  // And the map is told, because it cannot see this happen (M5-E4g,
  // #332). These are the automap's cells too, and everything below is a
  // call *into* the program, where the engine offers no points at all —
  // so neither the clear nor the roster's own return reaches the points
  // that seam watches its cells with.
  // `automap_state::note_panel_painted_over()` has the whole of that
  // argument. Free when the automap is off, whose panel has never been
  // open.
  box.automap().note_panel_painted_over();

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

/// The letters the two screenful-sized things this seam draws are paged
/// by (M5-E4e, #319): `N` for the next page and `P` for the one before
/// it, in both the cases a player's keyboard sends.
///
/// **They cost this seam no new claim.** The listing and a full-screen
/// page already take every keystroke there is while they are up, for the
/// reason `claimable::swallow` gives, so `N` and `P` were being taken and
/// dropped before this and are taken and acted on after it. Nothing else
/// on this machine sees a key it would have seen, which is why paging
/// needed no argument about which letters were free - the argument the
/// `Notes` splice had to make (#221) is about a bar the *program* is
/// reading, and neither of these screens is one.
constexpr std::uint8_t key_next_upper = 'N';
constexpr std::uint8_t key_next_lower = 'n';
constexpr std::uint8_t key_prev_upper = 'P';
constexpr std::uint8_t key_prev_lower = 'p';

/// What a key claimed at the program's own **blocking read** is answered
/// with: `key_ignored_scan` and `key_ignored_ascii`, which the automap
/// claims at the same address and so shares (`seam_key_read.h`, where the
/// argument is kept).

/// The letter the way out is named after, in both the cases a player's
/// keyboard sends. The screen says `EXIT`, and the letter of a word on a
/// bar is how every way out of every screen in this game is taken - so it
/// has to be one here too, and a player who reads the screen must not
/// have to guess at Escape. Since #317 it is the third word on both of
/// the full-screen shapes' bars rather than the only one on the
/// listing's.
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
  /// A screenful forward or back (M5-E4e, #319), on whichever of the two
  /// paged things is up: the log's own listing, or an entry drawn on the
  /// whole screen. `NEXT` and `PREV` on the bar both of them carry, and
  /// they stop at the ends rather than wrapping.
  page_next,
  page_prev,
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

/// The three words a screenful-sized reader carries on its bar, as
/// keystrokes (#317, #319). `none` for anything else, so a caller can go
/// on to whatever it does with a key it did not recognise.
///
/// Shared by the listing and a full-screen page because the two bars say
/// the same three words and mean the same three things by them - the only
/// difference is what a page *is* on each screen.
[[nodiscard]] claimable paging_key(std::uint16_t key) noexcept {
  const auto character = static_cast<std::uint8_t>(key & 0xFFU);
  if (character == key_next_upper || character == key_next_lower) {
    return claimable::page_next;
  }
  if (character == key_prev_upper || character == key_prev_lower) {
    return claimable::page_prev;
  }
  if (character == key_exit_upper || character == key_exit_lower) {
    return claimable::close;
  }
  return claimable::none;
}

[[nodiscard]] claimable claimable_of(std::uint16_t key,
                                     journal_reader_mode mode,
                                     journal_page_place place) noexcept {
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
    // looking at a list expects up and down to move in it - and since
    // #319 they move it a row at a time *within* a screenful, which is
    // what stops them being a scroll.
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
    // The three words on its own bar, by their first letters, which is
    // how every screen in this game is driven (#317).
    if (const claimable paged = paging_key(key); paged != claimable::none) {
      return paged;
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
  if (mode == journal_reader_mode::showing &&
      place == journal_page_place::screen) {
    // **A full-screen page swallows everything else**, for the listing's
    // own reason and not for a new one: the party's own command-bar
    // routine is live underneath it — that is *why* it is a full screen
    // (`journal.h`'s `bar_live()`) — so a key this seam left alone would
    // pick a command, or walk the party, on a screen nobody can see, and
    // the loop would paint its bar and its status line back over the page
    // to prove it. That is #230, exactly.
    //
    // The panel page makes no such claim and must not: it is opened by a
    // citation, in the middle of a story event, and the key that turns
    // the game's own page has to stay the game's. Which is also why the
    // three letters below are the *screen's* and never the panel's: a
    // panel is drawn beside a live command bar, and `E` on that bar is a
    // command of the program's.
    if (const claimable paged = paging_key(key); paged != claimable::none) {
      return paged;
    }
    return claimable::swallow;
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
                                  journal_page_place place,
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
  const claimable which = claimable_of(key, mode, place);
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

/// The reader's own command bar (M5-E4f, #317).
///
/// **Three words and their first letters**, which is the only way this
/// game is driven: every screen it has puts words on a row and takes the
/// word's initial - `EXIT`, `LOOK`, `ENCAMP`, `AREA` - and the two
/// commands this enhancement had already added were spliced onto the
/// program's own bars precisely so that they would look like the rest
/// (`Notes`, #221; `FIX`, `seam_encamp_fix.cpp`). The reader was the one
/// place that did not follow: it said `F1 MORE` and `ESC CLOSES`, which
/// names two keys this program has never asked anybody to press.
///
/// **The same bar on both full-screen shapes**, the log's listing and a
/// page of an entry, because on both of them the three words mean the
/// same three things - the next screenful, the one before it, and the way
/// out. Where they differ is only what a screenful *is*.
///
/// The panel keeps `F1 MORE` (`render()`), and that is a fact about the
/// screen rather than an oversight: a panel is drawn beside the program's
/// own live command bar, so `E`, `N` and `P` there are the *program's*
/// letters and taking them would pick commands off a bar the player can
/// still see. Only a screen that covers that bar may spell its keys as
/// words.
///
/// **Padded across all forty cells and drawn from column zero**, which is
/// not decoration. The frame's lower border stops at row `0x17` (above),
/// so row `0x18` is not painted by the box - and what is on it is the
/// adventuring screen's own command bar, which this screen is opened
/// from. The program's string drawer paints a cell rather than only its
/// lit pixels, so a line of spaces is the clear, and it costs no extra
/// call.
///
/// **Flush left and spaced one** (#329), which is what the program's own
/// bars are and what this one was not: it was indented by one and spaced
/// by three, so it did not line up with the bar it covers and did not
/// read as a bar this game drew. `ITEMS: BUY NEXT PREV EXIT` is the
/// program's own shop bar (`docs/playable.md`), and single spaces are its
/// rule as much as they are the party bar's.
///
/// The `n/m` after the words is a label and not a command, so it goes
/// after them and only when there is more than one page - the same rule
/// the panel's footer has always used, six spaces clear of the last word
/// so that nobody reads it as a fourth one. `+` after it is the delivery
/// buffer's own honesty: the entry was longer than the four kilobytes
/// that crossed the host boundary (journal.h), said rather than silently
/// stopped.
[[nodiscard]] list_line screen_bar(unsigned page, unsigned pages,
                                   bool truncated) {
  list_line line;
  for (std::size_t nth = 0; nth < bar_words.size(); ++nth) {
    if (nth != 0) {
      line.add(" ");
    }
    line.add(bar_words[nth]);
  }
  if (pages > 1) {
    line.add("      ");
    line.add(page + 1U);
    line.add("/");
    line.add(pages);
  }
  if (truncated) {
    line.add(" +");
  }
  line.pad_to(list_row_cells);
  return line;
}

/// The reader's own bar, onto the screen: **four calls and two colours**
/// (#330).
///
/// Every command bar this game draws paints the **initial white and the
/// tail green**, which is how a player is told which key picks the
/// command - and it is why the program's own bars are stored mixed case,
/// `Look`, `Encamp`, `Search`, with its drawer colouring by case. This
/// one was drawn in one call in the bright, so all three words were white
/// end to end and it was the one bar on the screen that did not follow.
///
/// **A seam cannot borrow that rule**, because the rule lives in the
/// program's drawer and this bar goes through the same drawer with a
/// colour argument: one call is one colour. So the line is drawn whole in
/// the green first - which is also the forty-cell clear the row needs -
/// and then the three initials are drawn over their own cells in the
/// bright. Four calls where there was one.
///
/// **The green line goes first and covers the whole row**, so a batch
/// that fills up half way through is not a half-white bar left standing:
/// the caller returns false, the next arrival starts this again from the
/// green, and every call here is an overdraw of the same cells. Nothing
/// here is idempotent by luck.
///
/// A batch queues twelve calls and places 256 bytes (`seam.h`), so the
/// three extra calls are three extra bytes of string each and are budgeted
/// where the rows are: `list_rows_per_pass` and `page_rows_per_pass`.
[[nodiscard]] bool draw_the_bar(seam_context& ctx, std::uint16_t image,
                                list_line& line) {
  if (!draw_line(ctx, image, line, bar_word_colour, list_exit_row,
                 list_exit_column)) {
    return false;
  }
  std::size_t column = list_exit_column;
  for (const std::string_view word : bar_words) {
    list_line initial;
    initial.add(word.substr(0, 1));
    if (!draw_line(ctx, image, initial, bar_key_colour, list_exit_row,
                   static_cast<std::uint16_t>(column))) {
      return false;
    }
    column += word.size() + 1U;  // the word, and the space after it
  }
  return true;
}

/// One pass of the journal's own screen. True when the screen is finished.
///
/// **Painted over several arrivals**, because one batch cannot hold it: a
/// batch queues twelve calls and places 256 bytes (`seam.h`), and a frame,
/// twenty rows of forty characters and a way out are far more than either.
/// So a
/// pass draws the frame if it has not been drawn, then as many rows as
/// fit, and says whether there is more to do. The program is sitting in
/// its own key loop while this happens and draws nothing itself, so a
/// screen that arrives in two pieces arrives in two pieces of one frame.
[[nodiscard]] bool draw_the_list(machine& box, seam_context& ctx) {
  journal_state& state = box.journal();
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::span<const journal_seen_row> rows = state.seen();
  std::size_t done = state.screen_drawn();

  if (done == 0) {
    // **The box's whole interior, cleared before the frame goes on it.**
    // This screen is also what a full-screen page of an entry comes back
    // to (#305), and the two carry titles of their own lengths and fill
    // as many of the twenty rows as they have - so without this the tail
    // of a longer title, and every row the shorter screen does not reach,
    // stay under the one that follows it.
    // Clearing and letting the program's own frame drawer put the
    // border and the title back leaves nothing of what was there, which
    // is the shape M5-E1e's residue (#298) taught.
    const std::array<std::uint16_t, 4> clear{
        list_frame_left, list_frame_top, list_frame_right, list_frame_bottom};
    if (!ctx.call_program(image, image_clear_region, clear)) {
      return false;
    }
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
      list_line bar = screen_bar(0, 1, false);
      return draw_the_bar(ctx, image, bar);
    }
  }

  // **A page of the log, and the whole of it** (M5-E4e, #319). It was a
  // window slid one row at a time to keep the cursor inside it, and it is
  // a screenful now, replaced whole: this program has no scrolling list
  // anywhere in it, and the entry pages this same reader draws in this
  // same box already step whole pages.
  //
  // **The page is derived and not kept**, which is one fewer thing that
  // can be wrong. It is the cursor's own page: the row the player is
  // pointing at decides which screenful is on the screen, so the
  // highlight can never be off it, and the page a player left is the page
  // they come back to without a second number that has to be kept
  // agreeing with the first. `list_cursor()` is clamped to the log
  // (journal.h), so this is too.
  const std::size_t cursor = state.list_cursor();
  const std::size_t top = (cursor / list_rows_visible) * list_rows_visible;
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
  state.set_screen_drawn(done);
  if (done < shown) {
    return false;
  }

  list_line bar =
      screen_bar(static_cast<unsigned>(cursor / list_rows_visible),
                 static_cast<unsigned>(list_pages(rows.size())), false);
  return draw_the_bar(ctx, image, bar);
}

/// A full-screen page's own numbers, beside the listing's.
///
/// The title and the body are the panel page's colours rather than the
/// listing's, which is #305's rule: the two sizes of one page should look
/// like one thing, and what changed there is how much room it has.
///
/// **The bottom row is not**, since #317. It is no longer a footer of grey
/// small print under a page: it is a bar of three words on the screen's
/// last row, which is where this game draws every bar it has - so it is
/// the listing's own bar, in the listing's own two colours, through
/// `draw_the_bar()`, and it has no colour of its own to name here any
/// more (#330). The panel's footer stays grey (`colour_footer`,
/// `render()`), because in the panel it really is a label under a page.
constexpr std::uint16_t page_title_colour = colour_title;
constexpr std::uint16_t page_body_colour = colour_body;

/// Where the body starts: below the row the frame writes its title on,
/// which is the row the listing starts its own rows at.
constexpr std::uint16_t page_first_row = list_first_row;

/// How many of its rows one pass paints.
///
/// Four rather than the listing's five, and the difference is the budget
/// rather than a preference: a page's last pass carries the bar as well,
/// and a title, four rows of thirty-eight characters and a forty-cell bar
/// is 222 of a batch's 256 bytes where five rows would have been 261. It
/// was 216 and 255 before the bar became four calls and two colours
/// (#330), which cost it the three initials' three strings of one
/// character: six bytes, and the second of those numbers went over.
constexpr std::size_t page_rows_per_pass = 4;

/// The title of a page: the section's own caption and its number, this
/// file's own characters, as the frame drawer takes them.
[[nodiscard]] list_line page_title(const journal_state& state) {
  list_line title;
  title.add(reader_word(state.entry().kind));
  title.add(" ");
  title.add(state.entry().number);
  return title;
}

/// Where a short line goes to be centred in the box's interior, on a
/// **character cell**: the grid the program sets all of its own text on.
[[nodiscard]] std::uint16_t page_centred_column(std::size_t width) {
  constexpr auto columns = static_cast<std::size_t>(screen_page.columns);
  const std::size_t take = width < columns ? width : columns;
  return static_cast<std::uint16_t>(list_frame_left + ((columns - take) / 2U));
}

/// The bottom row of a page: `screen_bar()`, which is the listing's own
/// bar and the whole of #317 (M5-E4f).
///
/// It said `1/3  F1 MORE   ESC CLOSES` and it says `NEXT PREV EXIT` now,
/// on the same row, in the same forty cells. What is new to a *reader* is
/// `PREV`: F1 walked forward and closed on the last page, so a person who
/// overshot had to leave the entry and open it again.
[[nodiscard]] list_line page_footer(unsigned page, unsigned pages, bool more,
                                    bool truncated) {
  return screen_bar(page, pages, !more && truncated);
}

/// One pass of a page of an entry, on the whole screen (M5-E4d, #305).
///
/// The listing's shape, for the listing's reasons: a batch queues twelve
/// calls and places 256 bytes (`seam.h`), a page is a frame and twenty
/// rows, so it is painted over successive arrivals while the program sits
/// in its own key loop drawing nothing. True when the page is finished.
///
/// **The interior is cleared and the frame redrawn on every page**, not
/// once per open. A page turn puts fewer lines on the screen than the one
/// before could have, and the box it is drawn in is the same box the
/// listing uses with a title of a different length - so anything left
/// standing would be read as part of the next page. Doing it per page
/// costs one call and needs no state to say what is already there, and
/// what it buys is that no arrangement of pages can leave a residue.
[[nodiscard]] bool draw_the_page(machine& box, seam_context& ctx) {
  journal_state& state = box.journal();
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  std::size_t done = state.screen_drawn();

  // What this page holds, worked out on every pass rather than kept: it
  // is a walk over at most four kilobytes, and it is also the answer F1
  // needs when it decides whether there is another page or a way out.
  const bool ready = state.delivery() == journal_delivery::ready;
  const std::string_view text = state.text();
  // Text pages and pictures together (#328), and the page clamped to
  // them, for `render()`'s reason: a page number can outlive the entry it
  // was right for.
  const reader_pages pages = count_pages(state, screen_page);
  const unsigned total = pages.total();
  const unsigned page = state.page() < total ? state.page() : total - 1U;
  const bool picture = page >= pages.text;
  const bool more = page + 1U < total;
  const page_walk walk =
      ready && !picture ? walk_pages(text, page, screen_page) : page_walk{};
  const page_layout laid = ready && !picture
                               ? lay_out(text, walk.start, screen_page)
                               : page_layout{};
  state.set_page_count(static_cast<std::uint16_t>(total));

  if (done == 0) {
    const std::array<std::uint16_t, 4> clear{
        list_frame_left, list_frame_top, list_frame_right, list_frame_bottom};
    if (!ctx.call_program(image, image_clear_region, clear)) {
      return false;
    }
    list_line title = page_title(state);
    std::uint16_t title_segment = 0;
    std::uint16_t title_offset = 0;
    if (!ctx.place_bytes(title.bytes(), title_segment, title_offset)) {
      return false;
    }
    const std::array<std::uint16_t, 8> frame{
        list_frame_left,   list_frame_top,   list_frame_right,
        list_frame_bottom, list_frame_style, page_title_colour,
        title_segment,     title_offset};
    if (!ctx.call_program(image, image_draw_frame, frame)) {
      return false;
    }
    if (picture) {
      // **The picture goes on the next arrival, not this one** (#328).
      // Everything above is a call *into* the program and lands when the
      // batch does; a picture is this handler's own writes and lands the
      // instant it runs. Drawn here it would go under the frame it was
      // drawn beside. So the bar goes on now, the box is left cleared,
      // and the arrival after this one paints into it — which is #303's
      // ordering with the program first and the seam after.
      list_line bar = page_footer(page, total, more, false);
      if (!draw_the_bar(ctx, image, bar)) {
        return false;
      }
      state.set_screen_drawn(1);
      return false;
    }
    if (!ready) {
      // The host had nothing, so the page says which nothing it was: two
      // short lines of this file's own words, centred the way the panel
      // centres them, and the same footer.
      const refusal what = refusal_for(state.delivery());
      unsigned nth = 0;
      for (const std::string_view line : {what.first, what.second}) {
        list_line said;
        said.add(line);
        static_cast<void>(
            draw_line(ctx, image, said, page_body_colour,
                      static_cast<std::uint16_t>(page_first_row + 8U + nth),
                      page_centred_column(line.size())));
        ++nth;
      }
      list_line footer = page_footer(page, total, more, false);
      return draw_the_bar(ctx, image, footer);
    }
  }

  if (picture) {
    // The second pass: the box is on the screen and this is what goes in
    // it. A picture the host had the count of and not the record is two
    // lines instead, the way the panel says the same thing.
    const unsigned nth = page - pages.text;
    if (art_is_here(state, nth)) {
      blit_art(box, state);
      return true;
    }
    unsigned line_nth = 0;
    for (const std::string_view line :
         {art_refusal.first, art_refusal.second}) {
      list_line said;
      said.add(line);
      static_cast<void>(
          draw_line(ctx, image, said, page_body_colour,
                    static_cast<std::uint16_t>(page_first_row + 8U + line_nth),
                    page_centred_column(line.size())));
      ++line_nth;
    }
    return true;
  }

  for (std::size_t drawn = 0; drawn < page_rows_per_pass && done < laid.lines;
       ++drawn, ++done) {
    const std::string_view line = laid.line[done];
    if (line.empty()) {
      continue;  // a paragraph break, on a row that was cleared
    }
    list_line row;
    row.add(line);
    if (!draw_line(ctx, image, row, page_body_colour,
                   static_cast<std::uint16_t>(page_first_row + done),
                   list_frame_left)) {
      break;  // the batch is full; the next arrival carries on from here
    }
  }
  state.set_screen_drawn(done);
  if (done < laid.lines) {
    return false;
  }

  list_line footer = page_footer(page, total, more, state.truncated());
  return draw_the_bar(ctx, image, footer);
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
  // The map, again, and this is the give-back that was found on a
  // display (M5-E4g, #332): the composer repaints the roster, which is
  // where the panel is drawn, and the automap's own points cannot see a
  // batch. Its panel stays *open* — the player asked for it and never
  // un-asked — so what it owes is the pixels, and its next arrival draws
  // them over the screen the program has just composed.
  box.automap().note_panel_painted_over();
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
  // And the entry's first picture, which is also how many it has (#328).
  //
  // **Asked here rather than at the first draw**, because the count is
  // half of how many pages the entry has and the reader needs that
  // before it draws anything — the footer says `1/3` on the page a
  // citation opens. Every answer carries the count, so an entry that is
  // prose costs one callout that answers zero and holds nothing.
  box.journal().ask_art(what, 0);
  (void)ctx.call_host(seam_host_service::journal_art,
                      journal_art_argument(what, 0));
  // Opening it is what takes the `*` off its line (#222). Only a line the
  // log already has: an entry the player asked for at the prompt was
  // never cited, so there is nothing to mark and nothing to write down.
  if (box.journal().mark_seen_read(what)) {
    tell_the_host_the_log_moved(box, ctx);
  }
}

/// Put the reader away. True when a give-back is on its way through a
/// batch, which is the caller's cue that nothing else happens this pass.
[[nodiscard]] bool close_reader(machine& box, seam_context& ctx,
                                std::uint16_t ds) {
  journal_state& state = box.journal();
  // **Anything of this seam's on the glass has to be given back, whether
  // or not the paint finished.** `on_screen()` says the *last* pass of a
  // screen ran; `screen_drawn()` says the first one did. A full screen is
  // painted over successive arrivals and a driven run measured a
  // twenty-row page taking about 230 frames to settle (#305), so a player
  // pressing Escape while it goes up is not an edge case — and until this
  // read both, that Escape closed the reader and left a half-drawn page
  // standing with nothing coming to repaint it. The listing has had the
  // same hole since #222, over a shorter window, and this closes it too.
  const bool was_up = state.on_screen() || state.screen_drawn() != 0;
  const journal_reader_mode mode = state.reader();

  // **A full-screen page opened from the listing goes back to the
  // listing** (#305), which is what a person paging through several
  // entries needs and what the panel page never had to decide, because
  // the listing was already gone from under it. Nothing is given back:
  // the same screen is still taken, the cursor is where it was - and so
  // therefore is the page of the log, since #319 derives the one from the
  // other - and the listing's own first pass clears the box before it
  // draws.
  if (mode == journal_reader_mode::showing && state.page_from_list()) {
    state.set_reader(journal_reader_mode::listing);
    state.clear_digits();
    return false;
  }

  // What has to be given back depends on what was taken: the listing and
  // a full-screen page took the whole screen, the panel took the roster's
  // cells, and asking the program to repaint more than was covered is the
  // M5-E2d bug.
  const bool took_the_screen =
      mode == journal_reader_mode::listing ||
      (mode == journal_reader_mode::showing &&
       state.page_place() == journal_page_place::screen);
  state.set_reader(journal_reader_mode::closed);
  state.set_page_place(journal_page_place::panel);
  state.set_page_from_list(false);
  state.clear_digits();
  if (!was_up) {
    return false;
  }
  if (took_the_screen) {
    give_the_screen_back(box, ctx);
  } else {
    give_the_roster_back(box, ctx, ds);
  }
  return true;
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

/// A screenful forward or back, on whichever of the two paged things is
/// up (M5-E4e, #319).
///
/// **They stop at the ends rather than wrapping**, which is the rule the
/// cursor this replaced followed and for the same reason: a list with a
/// top and a bottom that jumped from one to the other would lose a player
/// who was holding a key down.
///
/// **The listing's page is a jump of its cursor**, which is why turning
/// one needs no state of its own: the screenful on the screen is the
/// cursor's own page (`draw_the_list()`), so putting the cursor on the
/// first row of the next one *is* replacing the screenful. The move
/// clamps to the log, so `NEXT` onto a short last page lands on its last
/// row rather than past it - and that row is still on the page it was
/// asked for.
///
/// The page count is worked out here rather than kept, off the log as it
/// stands this instant: the log grows underneath a reader that is looking
/// at it, and a count remembered from the pass that drew the screen would
/// be a page short of the truth.
void turn_the_page(journal_state& state, int by) {
  if (state.reader() == journal_reader_mode::listing) {
    const std::size_t pages = list_pages(state.seen().size());
    const std::size_t at = state.list_cursor() / list_rows_visible;
    if (by > 0 ? at + 1U >= pages : at == 0) {
      return;  // stops at the ends rather than wrapping
    }
    const std::size_t want = by > 0 ? at + 1U : at - 1U;
    const auto here = static_cast<std::ptrdiff_t>(state.list_cursor());
    const auto there = static_cast<std::ptrdiff_t>(want * list_rows_visible);
    state.move_list_cursor(static_cast<int>(there - here));
    return;
  }
  if (by > 0) {
    if (state.page() + 1U < state.page_count()) {
      state.set_page(static_cast<std::uint16_t>(state.page() + 1U));
    }
    return;
  }
  if (state.page() != 0) {
    state.set_page(static_cast<std::uint16_t>(state.page() - 1U));
  }
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
      state.set_screen_drawn(0);
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
  if (state.page_place() == journal_page_place::screen) {
    // **On a full screen F1 is `NEXT` and nothing else** (M5-E4f, #317).
    // It used to turn the page and close on the last one, which was the
    // only way out a page named. A full screen carries `NEXT` and `EXIT`
    // as words of its own now, and a forward key that quietly became a
    // way out on the last page would contradict the bar the player is
    // reading - and would be a second forward key that stops somewhere
    // else than the first.
    turn_the_page(state, 1);
    return;
  }
  // In the panel it still does both, because the panel's footer still
  // says so: there is no room on twenty-two columns for three words, so
  // `F1 MORE` becomes `F1 CLOSES` on the last page and that is the whole
  // of what a citation's reader offers.
  if (state.page() + 1U < state.page_count()) {
    state.set_page(static_cast<std::uint16_t>(state.page() + 1U));
    return;
  }
  static_cast<void>(close_reader(box, ctx, ds));
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
  const claimable which =
      claim_key(box.processor(), ds, state.reader(), state.page_place(), key);
  claimed = which != claimable::none;
  switch (which) {
    case claimable::none:
      return false;
    case claimable::reader:
      press_reader_key(box, ctx, ds);
      // Closed means a give-back went out through a batch; a page that
      // went back to the listing (#305) did not, and its screen is drawn
      // on this pass.
      return state.reader() == journal_reader_mode::closed;
    case claimable::close:
      return close_reader(box, ctx, ds);
    case claimable::back:
      // Backspace: a digit rubbed out at the prompt, and a page back
      // anywhere else - which is the listing as well as an entry now
      // (#319). It used to step `page()` on the listing too, where
      // `page()` is the *entry's* page number and nothing on that screen
      // reads it: the key did nothing a player could see.
      if (state.reader() == journal_reader_mode::asking) {
        state.pop_digit();
      } else {
        turn_the_page(state, -1);
      }
      return false;
    case claimable::page_next:
      turn_the_page(state, 1);
      return false;
    case claimable::page_prev:
      turn_the_page(state, -1);
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
        // **The screen is not given back** (#305). The page is drawn in
        // the box the listing is drawn in, out of the same two routines,
        // so it takes that screen over rather than handing it back and
        // taking it again - and nothing is batched here, so the page is
        // painted on this same pass.
        //
        // What the old shape did is worth keeping in view, because it is
        // what this replaces: it composed the adventuring screen back and
        // then opened the entry in the roster panel, which meant the
        // page had to wait for the composer's batch to finish or be
        // painted over by it (#233).
        request(box, ctx, wanted);
        state.set_page_place(journal_page_place::screen);
        state.set_page_from_list(true);
        state.set_reader(journal_reader_mode::showing);
        state.set_page(0);
        return false;
      }
      if (const journal_citation wanted = state.asked(); wanted) {
        request(box, ctx, wanted);
        // **The prompt is in the panel wherever it is opened, and the
        // page it opens is a full screen only where one can be put
        // back** (#305): while the party's own command-bar routine is the
        // live one. F1 is claimed on every screen that has a roster, and
        // two of those are not that routine — the camp screen, whose menu
        // is a different call site, and an adventuring screen with a
        // vendor's bar up. A full screen on either is M5-E2d again.
        state.set_page_place(state.bar_live() ? journal_page_place::screen
                                              : journal_page_place::panel);
        state.set_page_from_list(false);
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

/// Make sure the buffer holds the picture the page that is up wants
/// (#328).
///
/// One picture at a time, because the buffer is the reader's whole box
/// packed and a page shows one; and a callout only when what is held is
/// not what is wanted, because a callout on every arrival would copy
/// twelve kilobytes at the program's own polling rate.
///
/// It also **clamps a page that has run past the end**, which is the one
/// place that can happen: the count comes from a host, and a host that
/// answered a count and then stopped answering — no store, a store
/// swapped under a running game — would leave the reader on a page
/// number nothing can draw. The last page is the honest answer, and it is
/// the same one `walk_pages()` gives for a text page past the end. It is
/// clamped against what the last render worked out rather than against a
/// fresh count, so the clamp costs nothing and lands one arrival later.
///
/// **The two early outs are the cost of this**, and they are why it may
/// run on every arrival at all: this point fires tens of times a frame,
/// and counting the pages means walking the whole delivered text. An
/// entry with no pictures — which is most of a journal — never gets that
/// far.
void fetch_art_if_wanted(machine& box, seam_context& ctx) {
  journal_state& state = box.journal();
  if (state.reader() != journal_reader_mode::showing) {
    return;
  }
  if (state.page_count() != 0 && state.page() >= state.page_count()) {
    state.set_page(static_cast<std::uint16_t>(state.page_count() - 1U));
    return;
  }
  if (state.art_count() == 0) {
    return;  // an entry that is prose: nothing to fetch and nothing to page to
  }
  const page_shape shape = state.page_place() == journal_page_place::screen
                               ? screen_page
                               : panel_page;
  const reader_pages pages = count_pages(state, shape);
  if (state.page() < pages.text) {
    return;  // a page of the entry's own text: no picture is wanted
  }
  if (state.page() >= pages.total()) {
    return;  // past the end; the clamp above catches it once a render says so
  }
  const unsigned nth = state.page() - pages.text;
  if (art_was_asked(state, nth)) {
    return;  // held, or asked for and not given: either way, asked
  }
  state.ask_art(state.entry(), static_cast<std::uint8_t>(nth));
  (void)ctx.call_host(
      seam_host_service::journal_art,
      journal_art_argument(state.entry(), static_cast<std::uint8_t>(nth)));
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

  // Before anything is measured against the last frame, because it can
  // change what would be drawn (#328).
  fetch_art_if_wanted(box, ctx);

  // Everything the panel is drawn from, as one number. The font pointer is
  // in it so a reader first drawn before the program installed its glyphs
  // gets them the moment it does.
  std::uint32_t drawn = 2166136261U;
  const auto mix = [&drawn](std::uint32_t value) noexcept {
    drawn = (drawn ^ value) * 16777619U;
    drawn ^= drawn >> 13U;
  };
  mix(static_cast<std::uint32_t>(state.reader()));
  // And which size a page is being drawn at (#305): the same entry on the
  // same page is a different screen in the panel and on the whole of it.
  mix(static_cast<std::uint32_t>(state.page_place()));
  // The list is drawn from the log and the cursor, so both are in the
  // signature: a line arriving at the top while the screen is up is a
  // screen that has to be drawn again, and the cursor is also what says
  // which page of the log is on it (#319).
  mix(static_cast<std::uint32_t>(state.seen().size()));
  mix(static_cast<std::uint32_t>(state.list_cursor()));
  mix(journal_open_argument(state.entry()));
  mix(state.page());
  mix(static_cast<std::uint32_t>(state.delivery()));
  // And the picture the buffer is holding (#328): a page that is a
  // picture is drawn from these three and from nothing else the mix
  // above already carries, so a picture arriving after the page it
  // belongs to went up is a screen that has to be drawn again.
  mix(static_cast<std::uint32_t>(state.art_count()));
  mix(static_cast<std::uint32_t>(state.art_nth()));
  mix(static_cast<std::uint32_t>(state.art_ready() ? 1U : 0U));
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

  // The listing and a full-screen page are not this seam's pixels at
  // all: the program draws both, out of the same two routines every Gold
  // Box screen is made of, so there is no buffer to rasterize and no font
  // to read (#222, #305).
  const bool by_the_program =
      state.reader() == journal_reader_mode::listing ||
      (state.reader() == journal_reader_mode::showing &&
       state.page_place() == journal_page_place::screen);
  if (by_the_program) {
    // A pass at a time. Until the last one the signature is left alone, so
    // the next arrival comes back here and carries on rather than deciding
    // the screen is already right.
    const bool finished = state.reader() == journal_reader_mode::listing
                              ? draw_the_list(box, ctx)
                              : draw_the_page(box, ctx);
    if (finished) {
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
  if (state.reader() != journal_reader_mode::showing) {
    // **A citation's page is in the panel, always** (#305). The watch
    // fires inside a script's own narration, where a vendor or an event's
    // NPC can be in the viewport, and the program's screen composer is
    // not a give-back that may be used there — that is M5-E2d exactly.
    // A page already up keeps the size it was opened at, because the game
    // citing something else is not a reason to move the screen under a
    // player's eyes.
    state.set_page_place(journal_page_place::panel);
    state.set_page_from_list(false);
  }
  // **Or the entry is a drawing** (#328). "Nothing rather than a blank
  // page" was the rule when text was the only thing an entry could be,
  // and an entry whose page is a map has pictures whether or not an OCR
  // engine was ever installed - a drawing has no words in it. So what
  // opens the reader is the host having *something*, and a page whose
  // text is a refusal and whose second page is the drawing is exactly
  // what such an entry is.
  const bool anything =
      state.delivery() == journal_delivery::ready || state.art_count() != 0;
  if (anything || was_open) {
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
  // **The party's own bar routine is live from here**, which is what
  // says a seam may take the whole screen and give it back (#305,
  // `journal.h`'s `bar_live()`). Recorded before anything is checked,
  // because it is a fact about where the machine is rather than about
  // whether the splice worked - and cleared at the return below, which
  // the routine always comes back through.
  box.journal().set_bar_live(true);
  box.journal().forget_bar_highlight();
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    return;
  }
  // **Where the highlight was before this call** (#330). The routine is
  // about to be handed a bar with one group more than the program's own,
  // and the only way it can move this byte is by matching a command - so
  // the value here is the value the seam-off run leaves when the command
  // matched is this seam's. Read before the splice, because after it the
  // bar is not the program's.
  box.journal().note_bar_highlight(cpu.read_byte(ds, data_bar_highlight));
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
///
/// **And the highlight comes off this seam's own command** (#330), which
/// is the second thing a spliced bar owes back. The routine sets the
/// shared highlight byte to the group of whatever command it matched, and
/// the group it is sitting on is the one drawn white end to end - so
/// choosing `Notes` left the byte on a group only this seam had put
/// there, and the bar came back with `Notes` in the highlight's white
/// while every word beside it wore the initial-white-and-green tail.
/// Measured with `--watch 6B2B` on a real run: `01` on the adventuring
/// bar after a load, `07` from the frame `N` was pressed, and `07` still
/// after the give-back, which is the maintainer's frame exactly.
///
/// **What it is put back to is what the routine was entered with**, and
/// that is #304's rule reduced to the case this bar has rather than a
/// different rule. The Fix is *inserted*, so every group after it is
/// renumbered and the way back is to step down by one; `Notes` is
/// *appended*, so groups one to six mean the same thing on both bars and
/// the only value that is not the program's is the last. What the program
/// would have left there is measurable: the byte moves only when the
/// routine matches a command, `N` matches none of the program's, and a
/// run with the seam off leaves it exactly as the routine found it. So
/// this reproduces the seam-off byte rather than approximating it.
///
/// **Only where the bar was spliced, and only for this seam's letter.**
/// `splice_out()` says whether the string that came back was the one this
/// seam wrote, `bar_highlight_known()` says whether the pass in was this
/// seam's, and nothing is written unless both hold and the command chosen
/// was `Notes` - so a run that never opens the journal never writes here.
void bar_after(machine& box, seam_context& ctx, std::uint16_t bar) {
  box.journal().set_bar_live(false);
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    return;
  }
  const bool was_spliced = splice_out(cpu, ds, bar);

  const cpu::registers& regs = cpu.regs();
  const std::uint8_t out_flag = cpu.read_byte(
      regs[cpu::sreg::ss],
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - frame_out_flag));
  if (out_flag != 0 || regs.get(cpu::reg8::al) != notes_key_ascii) {
    return;
  }
  journal_state& state = box.journal();
  if (was_spliced && state.bar_highlight_known()) {
    cpu.write_byte(ds, data_bar_highlight, state.bar_highlight());
  }
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
