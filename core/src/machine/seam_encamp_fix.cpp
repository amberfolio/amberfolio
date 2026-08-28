// SPDX-License-Identifier: AGPL-3.0-only
//
// The Encamp (F)ix: PLAN.md §5 item 4, M5-E1 (#172), reached the way the
// game's own commands are reached (M5-E1a, #186).
//
// PLAN.md §5 grants this one enhancement a deliberate exception: it
// automates play. The exception is narrow, and this file is written to
// stay inside it. **The game's own routines do the work; native code only
// asks.** Nothing here shortens a rest, heals a character, memorizes a
// spell, or suppresses an encounter. What it does is dial the game's own
// rest clock to the number of days the party needs and press the game's
// own Rest key, which is exactly the two things a player does by hand at
// the camp screen — and then it is out of the way while the program
// spends that time by its own rules.
//
// What #186 changed is not that. It is *how a person asks*. Until it, the
// asking was a host pull: a switch on a command line, or a key the host
// owns, outside the game. A command a player can only reach by typing at
// the emulator is not a command in the game; it is a console. So the
// command now sits on the camp screen's own command bar, one item longer
// than the game's, and a person uses it the way they use every other one.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// Addresses and offsets, which the clean-content rule allows and
// CONTRIBUTING.md names explicitly. Not a byte of the program is
// reproduced here, and neither is a word of its text — see
// `splice_in()`, which is written the way it is for exactly that reason.
//
// **Camp is a mode.** The data segment's game-mode byte reads 2 while the
// camp screen is up (`docs/playable.md` records the same byte and the
// same value, found by watching a run). The camp screen zeroes the rest
// clock on entry, says its line, and then runs a loop: build a prompt on
// its own stack, hand the prompt and a command bar to the program's
// menu-bar input routine, and act on the letter that comes back.
//
// **The command bar is a string, and that is the whole of this seam's
// display.** It is a Pascal string in the data segment — a length byte
// and its characters — in a 41-byte slot, so forty characters at most.
// The menu-bar routine draws every character it is given and treats each
// letter `A`-`Z` in it as a selectable command; a group of characters
// between letters is one highlightable word. So a seam that wants a
// command on that bar does not draw anything: it edits one string, and
// **the program draws the result in its own font, in its own colours,
// with its own highlighting**. Nothing is drawn that the game does not
// draw.
//
// **An unrecognised letter is already harmless.** The camp loop compares
// what comes back against its own commands — save, view, magic, rest,
// alter, exit — and, matching none of them, goes round the loop again. It
// is not a menu with an index that could run off the end; it is a
// sequence of comparisons against letters. So the program needs no
// defending from a letter it has never seen. The one thing this seam must
// not do is add a letter the program *does* recognise.
//
// **The rest clock is seven words in the data segment**, of which four
// are the ones the rest screen shows: minute units at +2, minute tens at
// +4, hours at +6, and days at +8 from `0x6DC2`. The camp screen zeroes
// the whole block on entry and the rest zeroes it again when it ends. The
// rest command's own set-up walks the party, takes the longest spell
// memorization time any member needs, and decomposes it into the three
// fields *below* days — **it never writes days**. The rest screen's own
// Inc key writes days and normalizes the carries; the program clamps the
// days field at 99, which is where `max_rest_days` comes from — it is the
// program's number, not one this file chose.
//
// **What a rest then does, per iteration**: takes five minutes off the
// clock, runs the per-minute memorization countdown over every member,
// runs the hourly one, and rolls the area's wandering-monster check when
// the area has one. Two of those matter here:
//
//   * a **heal tick** every 0x120 iterations — a rest day, since an
//     iteration is five minutes — applies one hit point to every member
//     through the program's own Cure Wounds applier, which refuses any
//     status outside {0, 1, 4, 5}, clamps at the record's maximum, and
//     promotes a dying character to unconscious and then revives them.
//     **One hit point per member per day, in parallel**, which is the
//     whole arithmetic this seam does;
//   * the memorization countdown **decrements one byte per member** every
//     iteration, from a block the rest zeroes before it starts.
//
// The rest ends when the clock runs out, when the player stops it, or
// when a wandering monster interrupts it — the last of which takes the
// party out of camp entirely.
//
//
// What the seam does: three points, and no memory of its own
// -----------------------------------------------------------
//
// All three are addresses in the overlay the camp screen lives in,
// resolved through the program's own note of where that overlay is
// (#131), so none of them is the address-free point this seam used to
// have and none of them has to guess whether acting is safe.
//
//   1. **Before the bar is handed over** (`camp_menu_before_input`).
//      Blank the prompt the loop has just built — six columns the longer
//      bar needs — and splice the Fix into the bar in place, before its
//      last group. The program then draws it.
//   2. **Where the menu-bar routine returns** (`camp_menu_after_input`).
//      Splice those characters straight back out, so that outside the one
//      call that drew the bar the program's own string is the program's
//      own string, byte for byte. Then, if what came back is a selected
//      command and it is the Fix's letter, do **one** thing and return:
//      spend a cure if there is one to spend and somebody to spend it on
//      (below), or else write the days field and post the camp bar's own
//      Rest key. The machine comes back to this instruction when a batch
//      of calls is done (seam.h), so the next arrival decides again from
//      what it then reads — which is what makes a loop out of a handler
//      that remembers almost nothing.
//   3. **At the rest command's entry** (`rest_command_entry`), reached
//      because of the key just posted. If the days field is non-zero,
//      post the rest screen's own Rest key.
//
// Then the program rests. Time passes on the game's calendar, pending
// spells are memorized at the game's own rate, hit points come back one a
// day through the game's own applier, and the game's own wandering
// monsters roll against the game's own odds. Nothing else is touched.
//
// **Point 3 has to know the rest about to start is this seam's**, and one
// word of the seam's own says so: point 2 sets `scratch_rest_is_ours` as
// it posts the Rest key, and point 3 reads it. No latch, no
// handler-local flag.
//
// **It was the program's own field until #192**, and the swap is worth
// keeping. The days field is zero whenever a rest begins — camp entry
// zeroes it, the end of a rest zeroes it, and the rest command's own
// set-up writes the three fields below it and never it. A player cannot
// have dialled days yet either: the Inc key that writes it lives on the
// rest screen, which the rest command has not drawn at the moment point 3
// runs. So a non-zero days field at the rest command's entry was a
// **signature the program itself kept**, read back out of the machine and
// remembered nowhere, which is the shape `docs/seams.md` §3 says to reach
// for first. It was the better design in every respect but one: the
// signature had to be non-zero, and keeping it so cost a whole party a
// day of their game (below).
//
// What the replacement costs is worth naming too, because §3 says to
// reach for a seam's own words *last*. This is sequence position, which
// is the one thing those words are explicitly not for, and it is
// configuration rather than machine state — the serialization never sees
// it. Nothing in this build notices: a recording replays from the start
// and a seam's words are dropped on `enable()`. It is a debt against a
// future where machine state is restored mid-sequence.
//
// That is also what disposes of the constraint that shaped the first cut
// of this seam (`docs/seams.md` §3): the program drains the keystroke
// buffer after every key it reads, so a handler that posts two keys posts
// one. Three points are three arrivals, and two keys posted one at each
// of two of them.
//
// **Spending cures, and giving every one of them back** (M5-E1b, #189).
// Before it rests, the Fix spends what the party already has: for each
// arrival, one Cure Light Wounds that some member holds *ready*, cast
// through the program's own cast driver — the same function the camp
// screen's own Cast command calls — at the worst-wounded member the
// program's own healing would accept. The roll, the overheal clamp, the
// forget and the drawing are all the program's.
//
// The rule of record is the player's loadout: **only cures a member
// already holds are spent, and one is queued back for every one spent**,
// by the two writes the program's own memorize command makes. Nothing
// fills an empty slot that was empty to begin with, and nothing is
// forgotten to make room. The queue-back happens *before* the cast, which
// is what makes the promise true at every instant rather than at the end
// — there is no moment at which the player is a cure down — and is why
// this handler needs no memory of what it has spent.
//
// **The days are the deficit plus one — and zero when there is no
// deficit.** The heal tick counts iterations in a counter the camp screen
// zeroes on entry and the rest does *not* reset between rests, so a
// second rest in one camp session starts part-way through a day and would
// come up one hit point short. A day of slack costs the player nothing
// they did not ask for — they asked to be whole — and removes the one way
// this arithmetic can be wrong.
//
// **A party that is whole gets no days at all**, and this seam shipped
// with that wrong. It used to dial at least one, so choosing the Fix with
// nobody hurt slept a full day for nothing. The day was never the
// arithmetic's: it was keeping point 3's signature non-zero, because
// point 3 used to infer "this rest is mine" from the clock. Point 3 reads
// a word of this seam's own now (`scratch_rest_is_ours`), the clock is
// free to say zero, and zero is a real answer — it leaves the duration
// the program's own wrapper computed, which is the rest the player's own
// Rest key would have given them.
//
// **And with nothing to rest for, the Fix does nothing at all.** Two
// things make a rest worth asking for: a hit point somebody is short, and
// a spell somebody is holding pending, which only time turns into one
// they can cast. With neither, the command declines and says so rather
// than spending the player's day to look busy.
//
//
// **And then it says what it did** (M5-E1b, #189). On the pass of the
// camp menu after the command finishes, the seam frames the program's own
// message panel and writes into it — through the program's own frame and
// string drawers (#188), so the box, the font, the colours and the
// centring of the title are the program's and nothing here rasterizes a
// glyph. The title is one of six, per way the command ended; the summary
// is the one line only this command can write, because every other number
// on that screen says where things are *now* and this is a difference
// against a before the machine has stopped holding; and what is left is
// the members it could not put right, named in the program's own word for
// their condition.
//
// **The way out is the bar under it.** The report is drawn before the
// camp screen's own command bar goes out, so what is on the screen is the
// box and a live bar with the program's own EXIT on it — any key the
// player presses takes them somewhere and takes the box with it. That is
// #186's rule one layer on: the prompt on screen is the prompt that
// works, and this report needs no prompt of its own to say so.
//
//
// What a later Gold Box title's FIX did that this one does not
// ------------------------------------------------------------
//
// #172 asks for this list at the point of definition. It was three items
// and M5-E1b (#189) closed one, so it is two:
//
//   * **it memorized cure spells for you**, filling empty slots with
//     cures so the rests were spent on healing magic;
//   * and in at least one title it **made room** by forgetting ready
//     spells that were not cures.
//
// The first is refused by the promise above rather than by taste. A seam
// that memorized cures into the player's slots would owe them their own
// loadout back afterwards, and leaving somebody's memorized spells
// replaced by cures because the mechanism could not undo it is precisely
// the kind of thing PLAN.md §5's native-feel rule refuses. The second
// this project would refuse anyway, for the reason the earlier
// implementation gave: the game has no by-hand forget, so a Fix that
// forgot spells would be changing the rules rather than saving
// keystrokes.
//
// **Casting used to be the third item on this list**, and the entry
// argued that a cast loop was a different shape of seam because it would
// owe the player their spells back. #189 landed it by never taking any:
// only cures a member already holds ready are spent, one is queued back
// before each is cast, and the loadout is equal at every instant rather
// than at the end. The objection was real and the answer was to remove
// what it objected to, which is worth remembering the next time a list
// like this one reads as settled.
//
// What the player keeps either way is the half that costs nothing:
// whatever they queued for memorization before pressing the Fix is
// memorized during the rest it pays for, by the program, on the program's
// own clock.
//
//
// What it is not yet
// ------------------
//
// **The fidelity test this seam owes is not the one every other seam
// owes, and #186 says so at length.** With the seam off, a run is byte
// for byte the run with no engine at all — unchanged, and PLAN.md §4's
// invariant. With it *on* and the Fix never used, the run is not
// identical, because the bar looks different, which is the entire point
// of the change. What holds instead, and what the tests assert, is that
// the difference is on the screen and nowhere else: between one menu draw
// and the next, **not one byte of the program's own memory differs from
// the run it would have been**, because the splice is undone at point 2
// and the prompt is a stack byte in a frame that is gone before the loop
// turns over.
//
// **A command the game interrupts does not report when it happened.**
// This is the one the report cannot reach, and it is worth being exact
// about because it is the common case for a wounded party. The box is
// drawn on the next pass of the camp menu, and a wandering monster —
// or, driven on a player's copy, the city watch rousting the party —
// takes the party out of camp entirely: the mode word goes to
// adventuring, the camp screen is gone, and the seam's three points are
// in the overlay it went with. The next pass of that menu is whenever
// the player next chooses ENCAMP, which may be an hour of their game
// later, and the box that greets them then would be an account of
// something they have half forgotten.
//
// So `Fix: Interrupted!` is a title this shape cannot honestly reach,
// and it is not in the list. What the player gets instead is the healing
// — which happened, and is on the roster panel — and no box. Closing it
// wants a fourth point, on the program's own way out of camp, and that
// is a seam with a different shape rather than a line of code here.
//
// **The elapsed time cannot express a rest of a day or more.** The
// game's clock is an hour and two minute digits with no day counter, so
// the summary drops its time clause whenever the command dialled days
// (`scratch_days_asked`) rather than print the remainder of a wrap as
// though it were an answer. A rest under a day — the memorization the
// cures queue back, which is the usual one — reports exactly.
//
// **Hit points have come back on a driven run**, which they had not when
// this comment was first written: `docs/playable.md` leg 7's second half
// drives a wounded party on a player's copy and watches two of the
// program's own cures land. What has *not* been driven is a party hurt
// badly enough that the cures run out and the days have to finish the
// job — so the arithmetic below, the worst deficit plus one, is still
// only exercised against rosters the unit suite writes. The mechanism
// has a public test either way
// (`tests/core/machine/seam_encamp_test.cpp` drives the handlers over a
// camp the test writes, and `tests/programs`' camp stand-in drives the
// same shape through the whole machine on all four targets), and one
// hit point per member per day remains a fact read off the program's own
// rest loop rather than a measurement. `docs/seams.md` §10 says which of
// the two each claim is.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about — the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> encamp_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The module the camp screen lives in -----------------------------------

/// Where the program keeps this module's load segment: the offset, in the
/// resident image, of one word (overlay.h, `seam_module::load_segment_at`).
/// It reads zero while the module is not loaded and the segment the module
/// begins at while it is, and the overlay manager maintains it — including
/// when it moves the module, which is the whole reason the field exists
/// (#131).
///
/// Found and checked the way `seam_cheats.cpp` documents for its own
/// module: search the resident image for the manager's record whose file
/// offset and length are the two below — one match — and take the word
/// sixteen bytes past its start. That method was verified here by running
/// it against the module `seam_cheats.cpp` already had facts for and
/// getting `0x360` back.
constexpr std::uint32_t overlay_load_segment_at = 0x760;

/// The module the camp screen and the rest command live in: one read,
/// whose facts the tracker records (overlay.h), plus the word above. The
/// digest is of those bytes as read, so a copy whose overlay file does not
/// match is not this module.
constexpr seam_module camp_module{
    .file = "GAME.OVR",
    .file_offset = 96305,
    .length = 8158,
    .digest =
        "9b9153998d07b8e16d2466d95670cdd683e4dc587281e162d05d15c3458d362b",
    .load_segment_at = overlay_load_segment_at};

/// In the camp menu loop, the instruction after the prompt has been built
/// and before the bar is handed to the menu-bar input routine. Offsets
/// from the start of the module, which is to say from the segment the word
/// above holds, wherever the manager has most recently put it.
constexpr std::uint32_t camp_menu_before_input = 0x1F06;

/// In the same loop, where the menu-bar input routine returns: the
/// instruction that stores the letter it answered with. The point runs
/// before that instruction, so the letter is still in AL.
constexpr std::uint32_t camp_menu_after_input = 0x1F24;

/// The rest command's entry — the routine the camp bar's own Rest letter
/// dispatches to, which computes the memorization time and then runs the
/// rest screen.
constexpr std::uint32_t rest_command_entry = 0x077A;

// --- The camp loop's own stack frame ---------------------------------------
//
// Offsets from BP inside the camp menu loop, at the two points above.
// Both are read at `SS:BP - offset`.

/// The prompt the loop builds on every pass, as a Pascal string: its
/// length byte is here. Blanking it is what frees the columns the longer
/// bar needs.
constexpr std::uint16_t frame_prompt = 0x0B;

/// The byte the menu-bar routine sets to say what kind of answer it is
/// giving: zero for a command selected off the bar, non-zero for a raw or
/// extended key it is passing through. The Fix is only ever the first.
constexpr std::uint16_t frame_out_flag = 0x04;

// --- The program's data segment --------------------------------------------
//
// Offsets in the data segment DS addresses throughout the program. The
// first two are already in this tree (`seam_cheats.cpp`, and the mode
// byte is `docs/playable.md`'s `49F3`); the rest are the camp screen's.

/// The game mode byte, and the value it reads while the camp screen is up.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_camp = 2;

/// The roster list's head: a far pointer, offset then segment. The party,
/// which is the list the rest's heal tick walks.
constexpr std::uint16_t data_roster_head = 0x5D96;

/// The camp screen's command bar: a Pascal string, length byte first.
constexpr std::uint16_t data_camp_bar = 0x508;

/// How long that string may be. The slot it sits in is 41 bytes — its
/// length byte and forty characters — which is how the program's Pascal
/// strings are laid out and is also, not by coincidence, the width of the
/// screen in characters. The bar the program ships is well short of it,
/// and the Fix costs four; the check below is against the slot and not
/// against what happens to be in it, because the slot is the fact.
constexpr std::uint8_t camp_bar_capacity = 40;

/// The days field of the rest clock — the one word this seam writes.
constexpr std::uint16_t data_rest_days = 0x6DC2 + 8;

/// What the program clamps the days field to when its own Inc key
/// normalizes the clock. **Its number, not this file's**, which is what
/// makes it the right backstop: dialling past it would be dialling
/// something the program would not have let a player dial, and the rest
/// screen's two-digit display could not show it either.
constexpr std::uint16_t max_rest_days = 99;

// --- The magic the Fix spends, and gives back -------------------------------

/// Cure Light Wounds, by the id the program's own spell tables use.
///
/// The only heal a player character can hold in memory: the program's own
/// memorize picker scans ids 1..0x37, and its other healing handlers sit
/// above that range as item and monster effects. A table rather than a
/// constant, because a later mode may spend more than one kind and the
/// shape of the code should not have to change for it.
constexpr std::array<std::uint8_t, 1> heal_spells{3};

/// The memorized-spell slots in a character record: twenty-one bytes, one
/// spell id each, zero for an empty one. **Bit 7 set means pending** —
/// queued for memorization and not learned yet — and clear means ready to
/// cast. The program's own memorize command writes `id | 0x80` into the
/// first empty slot and then sorts them; its cast driver clears the slot
/// it spends.
constexpr std::uint16_t rec_spell_slots = 0x17;
constexpr unsigned rec_spell_slot_count = 0x15;
constexpr std::uint8_t spell_pending = 0x80;

/// Whether this character may act: the byte the program's own screens
/// gate on, beside a status that is not an animated body.
constexpr std::uint16_t rec_can_act = 0x10D;
constexpr std::uint8_t status_animated = 1;

/// The area the party is in, as a far pointer, and the word in it that
/// says this area refuses spellcasting — which the program's own cast
/// screen reads before it offers anything.
constexpr std::uint16_t data_area_record = 0x49D2;
constexpr std::uint16_t area_refuses_casting = 0x01CA;

/// The current party member, as a far pointer: what the program's own
/// cast screen sets before it casts, and what its slot sort works on.
constexpr std::uint16_t data_current_member = 0x5D92;

/// The cast target anchor, as a far pointer. The program's own target
/// picker opens **already on** the member this names — which is what a
/// player's own positioning keys do — so one keystroke finishes the pick.
constexpr std::uint16_t data_cast_anchor = 0x6DB5;

/// That keystroke.
constexpr std::uint8_t pick_key_scancode = 0x1F;
constexpr std::uint8_t pick_key_ascii = 'S';

// --- The program's own routines this seam calls (#188) ---------------------
//
// Offsets from the image base, except the last, which is in the same
// overlay this seam's points are in. Each was read off the routine itself,
// and what each cleans up is part of the fact: the program's routines are
// far and pop their own arguments.

/// The generic cast driver, through the resident thunk that loads its
/// overlay — never the overlay address, which would be a call into code
/// that may not be in memory. Takes a far out-flag, an announce flag, a
/// skip-abort flag and the spell id; cleans ten bytes.
constexpr std::uint16_t image_cast_driver = 0x0999;

/// The border and the clear the program's own cast screen draws before it
/// casts, so that what the player sees is what that screen shows.
constexpr std::uint16_t image_frame_border = 0x3F10;
constexpr std::uint16_t image_clear_region = 0x4047;

/// The slot sort, in this seam's own module: it orders the current
/// member's slots by id, which is what the program's own memorize command
/// does after it writes one. No arguments, and it cleans none.
constexpr std::uint32_t module_sort_spell_slots = 0x0824;

/// The region the cast screen frames and clears, in character cells.
constexpr std::uint16_t cast_region_style = 0;
constexpr std::uint16_t cast_region_bottom = 0x16;
constexpr std::uint16_t cast_region_right = 0x26;
constexpr std::uint16_t cast_region_top = 0x11;
constexpr std::uint16_t cast_region_left = 1;

/// The framed box with a centred title, and the string drawer — the two
/// routines the report is made of (#188, `docs/seams.md` §3).
///
/// The frame puts its title on the box's **first interior row** rather
/// than on the border, which is why the title's row and the box's top are
/// the same number below.
constexpr std::uint16_t image_draw_frame = 0x041F8;
constexpr std::uint16_t image_draw_string = 0x076B6;

/// The report's box is the message panel, which is the same region the
/// cast screen frames — so the constants above are these, and the report
/// says so by using them rather than repeating them.
///
/// There is no constant for the title row, and its absence is the fact:
/// the frame puts its title on the box top itself, so the row the title
/// lands on is `cast_region_top` and naming it twice would invite the two
/// to drift apart.
constexpr std::uint16_t report_summary_row = cast_region_top + 1;
constexpr std::uint16_t report_first_row = cast_region_top + 2;

/// Where the three cells of an exception line begin, and how wide the
/// first two are. The name is the full width a character name can be; the
/// hit points are `255/255` at their longest.
constexpr unsigned report_name_column = 1;
constexpr unsigned report_name_width = 0x0F;
constexpr unsigned report_points_column = 0x11;
constexpr unsigned report_reason_column = 0x19;

/// The widest title the frame will centre without painting over its own
/// border: the centring is `(left + right - length) / 2`, so a title of
/// the full interior width lands one column outside it.
constexpr unsigned report_title_width = cast_region_right - cast_region_left;

/// The colours, which are the program's own: the frame and its title as
/// the program frames every other box, the body as its message text, and
/// the one it uses for a thing the player should notice.
constexpr std::uint16_t report_frame_colour = 0x0F;
constexpr std::uint16_t report_body_colour = 0x0A;
constexpr std::uint16_t report_warning_colour = 0x0E;

/// The game's clock, three words in the area record: the hour, and the
/// minutes as tens and units. The same three the program's own status
/// line reads, and the reason the report can say how long the rests took
/// when the clock on screen can only say what time it is now.
constexpr std::uint16_t area_clock_minute_units = 0x018E;
constexpr std::uint16_t area_clock_minute_tens = 0x0190;
constexpr std::uint16_t area_clock_hours = 0x0192;

/// Minutes in a day, for a run that crosses midnight — the clock carries
/// no day counter here, so an elapsed time that reads backwards is one
/// wrap and not a fault.
constexpr unsigned minutes_in_a_day = 24 * 60;

/// The program's own wound-status names, and the stride between them: a
/// table of Pascal strings in the data segment, indexed by the status
/// byte. The report names a member resting cannot help **in the
/// program's own words**, by handing the drawer a pointer into this table
/// — never by copying the text through this seam, which is the same rule
/// the bar splice follows (§8.1).
constexpr std::uint16_t data_status_names = 0x1047;
constexpr std::uint16_t status_name_stride = 0x0D;

/// This seam's own words (seam.h): how many cures it has spent since the
/// command began. Zero between commands, and the backstop below is read
/// against it.
constexpr unsigned scratch_casts = 0;

/// What the command is doing, and — when it has finished — how it
/// finished, so that the next pass of the camp menu can say so.
///
/// This is the one word here that is a **state machine** rather than a
/// measurement, and §3 says to reach for a seam's own words last. The
/// justification is the same as the report's: what the command *did* is a
/// comparison against a before that the machine has stopped holding, and
/// which of the outcomes below it reached is part of that comparison.
constexpr unsigned scratch_state = 2;

/// The party's total hit points when the command began, and the game
/// clock in minutes at the same instant. Both are gone from the machine
/// by the time the report is drawn, which is exactly what these words are
/// for (§8.2).
constexpr unsigned scratch_points_before = 3;
constexpr unsigned scratch_clock_before = 4;

/// The days the command dialled, kept from the ask to the report.
///
/// **Not for the arithmetic — for knowing when not to print a number.**
/// The game's clock is an hour and two minute digits and carries no day
/// counter (`area_clock_*` below), so a difference between two readings
/// can only express less than a day. A rest of one day or more comes back
/// with a clock that says something true about the time of day and
/// nothing about how long the party slept, and the summary drops its
/// elapsed clause rather than print the remainder as though it were the
/// answer. A wrong number is worse than no number, and the days the
/// command asked for are on the game's own calendar anyway.
constexpr unsigned scratch_days_asked = 5;

/// How the command is going, in `scratch_state`.
///
/// **Idle is zero**, so a seam that has just been switched on — its words
/// start empty (seam.h) — is a seam with no report owing.
/// A byte is the base, and `as_word()` below is how it reaches a scratch
/// word: the outcomes number in the single digits, and the word they are
/// kept in is the engine's type rather than this one's.
enum class run_state : std::uint8_t {
  idle = 0,
  /// Chosen, and working: casting a cure per arrival.
  running = 1,
  /// A rest has been asked for. **The outcome is not known yet**, because
  /// what a rest achieves is only readable after it — so this state says
  /// "decide when the camp menu comes back", and the deciding happens
  /// there, out of the machine, rather than being guessed here.
  resting = 2,
  /// Finished. Each of these is a title, and the next arrival at the camp
  /// menu draws it.
  healed = 3,
  rest_stopped = 4,
  no_cure_memorized = 5,
  nobody_knows_a_cure = 6,
  cannot_cast_here = 7,
  player_stopped = 8,
};

[[nodiscard]] constexpr std::uint16_t as_word(run_state which) noexcept {
  return static_cast<std::uint16_t>(which);
}

/// Set when this seam has posted Rest at the camp bar, cleared when the
/// rest command starts — so point 3 knows the rest about to happen is
/// **this seam's** and not one the player asked for.
///
/// It used to be inferred from the days field being non-zero, and that
/// cost the player a day: a whole party needs no days at all, so the
/// arithmetic was forced never to return zero purely to keep the
/// signature alive. A word of the seam's own says the same thing without
/// charging anybody for it, which is what `scratch_words` is for — a
/// thing the machine does not hold (#189).
constexpr unsigned scratch_rest_is_ours = 1;

/// How many cures one command may spend before this seam stops trying.
///
/// **A backstop and not the design.** Every cast spends a ready cure, so
/// the loop ends on its own; this is what stops a cast the program
/// *refuses* — one whose picker never answers — from being asked for
/// again at every arrival for ever. Two a member for a party of six is
/// past any real loadout.
constexpr std::uint16_t max_casts = 16;

/// How many records a roster walk will follow before giving up. A party
/// is six with room for hangers-on; sixteen is several times that and is
/// what keeps a record whose next pointer is not what the facts say from
/// spinning the host.
constexpr unsigned max_roster_walk = 16;

// --- The character record ------------------------------------------------
//
// Offsets into a character record. `rec_next_offset`, `rec_status` and
// `rec_hit_points` are `seam_cheats.cpp`'s, restated rather than shared
// because they are facts about the program and not a module this file
// depends on.

constexpr std::uint16_t rec_max_hit_points = 0x32;

/// The known-spell flags: one byte per spell id, **one-based**, so the id
/// that indexes them is never zero and index zero is `rec_max_hit_points`
/// above. The overlap is the program's own and not a mistake in reading
/// it — the two facts were found separately and agree.
///
/// This is what tells "nobody has a cure memorized" apart from "nobody
/// knows one", which are different sentences and different titles: a
/// party whose cleric knows Cure Light Wounds and has not memorized any
/// can do something about it, and one whose nobody knows it cannot.
constexpr std::uint16_t rec_known_spells = 0x32;
constexpr std::uint16_t rec_next_offset = 0x104;  // far ptr: next record
constexpr std::uint16_t rec_status = 0x10C;       // wound status
constexpr std::uint16_t rec_hit_points = 0x11B;   // current hit points

/// Whether resting can put this character right, by the wound status:
/// the set the program's own healing applier gates on — unhurt, an
/// animated body, knocked out, or bleeding out. Everything else — slain,
/// petrified, gone — is refused by that applier, so a seam that counted
/// such a character's deficit would be asking the party to rest for a
/// number of days that could never come down.
///
/// This is the fact in this file most worth getting right, and it is the
/// same shape as the one `seam_cheats.cpp` warns about: the byte beside
/// the status reads like a flag and is not one. Here it decides how long
/// the party sleeps.
[[nodiscard]] constexpr bool heals_by_resting(std::uint8_t status) noexcept {
  return status == 0 /* unhurt */ || status == 1 /* animated */ ||
         status == 4 /* unconscious */ || status == 5 /* dying */;
}

// --- The command this seam puts on the bar ---------------------------------

/// The Fix, as it appears on the bar: a separator and the word, four
/// characters, **written here and nowhere read from the program**. The
/// enhancement's own name (PLAN.md §5 item 4), in the case the program's
/// own bar uses — one capital, which is what makes it one command letter
/// and one highlightable group, and the rest lower case so they are not
/// letters the menu-bar routine could select on.
constexpr std::array<std::uint8_t, 4> fix_item{' ', 'F', 'i', 'x'};

/// Its length, as the arithmetic below wants it.
constexpr unsigned fix_item_length = 4;

/// The key the menu-bar routine answers with when the Fix is chosen, and
/// the one letter this seam had to be sure the camp loop does not already
/// use. It compares against six letters and this is not one of them.
constexpr std::uint8_t fix_key_ascii = 'F';

/// The key the camp bar and the rest screen both take for Rest, as INT 16h
/// hands it back: the scan code for R and its character. The program
/// uppercases what it reads before matching it against a bar, so this is
/// the same key a player types.
constexpr std::uint8_t rest_key_scancode = 0x13;
constexpr std::uint8_t rest_key_ascii = 'R';

/// The separator the program's bars put between commands, and the one this
/// seam looks for to find where the last command begins. A space, which is
/// what the menu-bar routine's own grouping rule makes it.
constexpr std::uint8_t bar_separator = ' ';

// --- Reading the machine ---------------------------------------------------

[[nodiscard]] std::uint16_t word_after(std::uint16_t at,
                                       std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(at + by);
}

/// Whether `segment:offset` is inside conventional RAM — 00000-9FFFF, the
/// memory a program's own structures live in (memory_map.h) — and so
/// whether this seam is willing to read it at all.
///
/// **Checked before every read this file makes, and that is not
/// belt-and-braces.** A read through the bus is a bus cycle: above
/// conventional memory it is the video window, where a read loads the
/// adapter's latches (ega.h), and a guard that perturbed the machine it is
/// inspecting would be doing the one thing a seam may never do. The reads
/// here are made from points with addresses, so DS is the program's — but
/// the roster walk follows a far pointer it read out of memory, and that
/// is the read this has always been for. It caught a real one: driven
/// against the program, the address-free version of this seam left seven
/// `unmapped_memory_read` notices behind it, walking a roster out of a
/// data segment that was not the program's. Nothing was corrupted and
/// nothing was faked; the machine said, correctly, that something had
/// touched memory nobody answers for, and the something was this seam.
[[nodiscard]] bool in_conventional_ram(std::uint16_t segment,
                                       std::uint16_t offset) noexcept {
  return cpu::physical_address(segment, offset) < conventional_ram_size;
}

/// One byte at `segment:offset`, or false and nothing read.
[[nodiscard]] bool read_byte(cpu::processor& cpu, std::uint16_t segment,
                             std::uint16_t offset, std::uint8_t& out) {
  if (!in_conventional_ram(segment, offset)) {
    return false;
  }
  out = cpu.read_byte(segment, offset);
  return true;
}

/// One word, and both of its bytes are checked: a word at 9FFFF is half
/// in the video window.
[[nodiscard]] bool read_word(cpu::processor& cpu, std::uint16_t segment,
                             std::uint16_t offset, std::uint16_t& out) {
  if (!in_conventional_ram(segment, offset) ||
      !in_conventional_ram(segment, word_after(offset, 1))) {
    return false;
  }
  out = cpu.read_word(segment, offset);
  return true;
}

/// One byte written, or false and nothing written. The write goes through
/// the bus like the program's own, which is why it is refused rather than
/// made when it would land somewhere a seam has no business writing.
[[nodiscard]] bool write_byte(cpu::processor& cpu, std::uint16_t segment,
                              std::uint16_t offset, std::uint8_t value) {
  if (!in_conventional_ram(segment, offset)) {
    return false;
  }
  cpu.write_byte(segment, offset, value);
  return true;
}

// --- The bar ---------------------------------------------------------------

/// Where the Fix sits in the bar right now: the one-based index of its
/// separator, or zero if it is not there.
///
/// Both a test for "is it already spliced in" and the answer to "where do
/// I take it out from", which is why it is one function. The scan is over
/// a string of at most forty characters at a point that runs once per menu
/// draw.
[[nodiscard]] unsigned find_fix(cpu::processor& cpu, std::uint16_t ds,
                                std::uint8_t length) {
  if (length < fix_item_length) {
    return 0;
  }
  for (unsigned at = 1; at + fix_item_length - 1 <= length; ++at) {
    bool all = true;
    for (unsigned nth = 0; nth < fix_item_length && all; ++nth) {
      std::uint8_t byte = 0;
      all = read_byte(
                cpu, ds,
                word_after(data_camp_bar, static_cast<std::uint16_t>(at + nth)),
                byte) &&
            byte == fix_item[nth];
    }
    if (all) {
      return at;
    }
  }
  return 0;
}

/// Put the Fix on the bar, before its last command. False and nothing
/// written if the string is not the shape the facts say — too long to take
/// four more characters, or with no separator to insert at, or already
/// carrying the Fix.
///
/// **Nothing here is composed out of the program's own words.** The seam
/// does not know what the bar says and does not need to: it finds the last
/// separator and inserts four characters of its own before it. That is why
/// this file can be in a public repository at all, and it is the shape any
/// later seam that adds a command to one of this program's menus should
/// copy (`docs/seams.md` §3).
[[nodiscard]] bool splice_in(cpu::processor& cpu, std::uint16_t ds) {
  std::uint8_t length = 0;
  if (!read_byte(cpu, ds, data_camp_bar, length) || length == 0 ||
      length > camp_bar_capacity ||
      length + fix_item_length > camp_bar_capacity) {
    return false;
  }
  if (find_fix(cpu, ds, length) != 0) {
    return false;  // already there: this pass is not the first.
  }

  unsigned last = 0;
  for (unsigned at = 1; at <= length; ++at) {
    std::uint8_t byte = 0;
    if (!read_byte(cpu, ds,
                   word_after(data_camp_bar, static_cast<std::uint16_t>(at)),
                   byte)) {
      return false;
    }
    if (byte == bar_separator) {
      last = at;
    }
  }
  if (last == 0) {
    return false;  // one command, or not a bar: not something to add to.
  }

  // Backwards, so the move cannot overwrite what it has not read yet.
  for (unsigned at = length; at >= last; --at) {
    std::uint8_t byte = 0;
    if (!read_byte(cpu, ds,
                   word_after(data_camp_bar, static_cast<std::uint16_t>(at)),
                   byte) ||
        !write_byte(cpu, ds,
                    word_after(data_camp_bar, static_cast<std::uint16_t>(
                                                  at + fix_item_length)),
                    byte)) {
      return false;
    }
  }
  for (unsigned nth = 0; nth < fix_item_length; ++nth) {
    if (!write_byte(
            cpu, ds,
            word_after(data_camp_bar, static_cast<std::uint16_t>(last + nth)),
            fix_item[nth])) {
      return false;
    }
  }
  return write_byte(cpu, ds, data_camp_bar,
                    static_cast<std::uint8_t>(length + fix_item_length));
}

/// Take it back out again, leaving the program's own string exactly as it
/// was. False if it was not there, which is not an error: the pass that
/// could not splice it in is the pass that has nothing to take out.
[[nodiscard]] bool splice_out(cpu::processor& cpu, std::uint16_t ds) {
  std::uint8_t length = 0;
  if (!read_byte(cpu, ds, data_camp_bar, length) ||
      length > camp_bar_capacity) {
    return false;
  }
  const unsigned at = find_fix(cpu, ds, length);
  if (at == 0) {
    return false;
  }
  for (unsigned nth = at + fix_item_length; nth <= length; ++nth) {
    std::uint8_t byte = 0;
    if (!read_byte(cpu, ds,
                   word_after(data_camp_bar, static_cast<std::uint16_t>(nth)),
                   byte) ||
        !write_byte(cpu, ds,
                    word_after(data_camp_bar, static_cast<std::uint16_t>(
                                                  nth - fix_item_length)),
                    byte)) {
      return false;
    }
  }
  return write_byte(cpu, ds, data_camp_bar,
                    static_cast<std::uint8_t>(length - fix_item_length));
}

// --- Reading the party -----------------------------------------------------

/// One member, as the walk found them.
struct member_reading {
  std::uint16_t offset{};
  std::uint16_t segment{};
  /// How far below their maximum they are, if resting can put that
  /// right. Zero for anyone it cannot help at all.
  unsigned deficit{};
  /// Able to act, and so able to cast: the program's own gate.
  bool can_act{false};
  /// Ready cures held — slots whose id is a heal and whose pending bit
  /// is clear.
  unsigned ready_cures{};
  /// The first empty slot, or `no_slot`: where a cure this seam spends
  /// is queued back.
  unsigned free_slot{};
  bool has_free_slot{false};
  /// Cures held **pending** — queued for memorization, and so not
  /// spendable now. What tells "nobody memorized one" apart from "nobody
  /// knows one", which are different sentences and different titles.
  unsigned pending_cures{};
  /// Hit points now and at most, as the report prints them. Read for
  /// everybody, not only for the members resting can help: a line naming
  /// somebody the command could not touch still has to say where they
  /// are.
  std::uint8_t points{};
  std::uint8_t most_points{};
  /// The wound status, for the program's own word for it.
  std::uint8_t status{};
  /// Whether resting can put this one right at all — the applier's gate,
  /// kept because the report's reason column turns on it.
  bool heals{false};
};

/// What one walk of the roster found.
struct roster_reading {
  /// The list was a list: every link a far pointer, and it ended within
  /// `max_roster_walk` rather than being cut off by it.
  bool ended{false};
  unsigned members{0};
  /// The largest hit-point deficit over the members resting can put
  /// right. Zero when the party is whole, or when nobody on it can be
  /// healed at all.
  unsigned worst_deficit{0};
  /// Slots anybody is holding pending — a spell queued for memorization,
  /// which only time turns into one they can cast. Something to rest
  /// *for* even when every hit point is where it should be.
  unsigned pending_spells{0};
  /// The party's hit points added up, which is the number the report's
  /// summary is a difference of.
  unsigned points{0};
  /// Cures the party is holding, ready and pending. The ready count says
  /// whether anybody could cast one now.
  unsigned ready_cures{0};
  unsigned pending_cures{0};
  /// Whether anybody who can act knows a cure at all, book flag rather
  /// than memorized — the difference between a party that can do
  /// something about it and one that cannot.
  bool anybody_knows_a_cure{false};
  std::array<member_reading, max_roster_walk> member{};
};

/// Whether this member knows any of the cures at all — the book flag,
/// which is a different question from holding one memorized. False when
/// the read is refused, which is the fail-closed direction: a report that
/// said nobody knows a cure because it could not look would be worse than
/// one that said the rest was stopped.
[[nodiscard]] bool knows_a_cure(cpu::processor& cpu, std::uint16_t segment,
                                std::uint16_t offset) {
  for (const std::uint8_t heal : heal_spells) {
    std::uint8_t known = 0;
    if (read_byte(cpu, segment,
                  word_after(offset, static_cast<std::uint16_t>(
                                         rec_known_spells + heal)),
                  known) &&
        known != 0) {
      return true;
    }
  }
  return false;
}

/// Whether `id` is one of the spells this seam is willing to spend.
[[nodiscard]] constexpr bool is_heal_spell(std::uint8_t id) noexcept {
  for (const std::uint8_t heal : heal_spells) {
    if (id == heal) {
      return true;
    }
  }
  return false;
}

/// Walk the party from the head word, reading only. Every read goes
/// through the bus, at the offsets the program itself reads them at, and
/// every one of them is refused rather than made when it would land
/// outside conventional memory.
[[nodiscard]] roster_reading read_roster(cpu::processor& cpu,
                                         std::uint16_t ds) {
  roster_reading out;
  std::uint16_t offset = 0;
  std::uint16_t segment = 0;
  if (!read_word(cpu, ds, data_roster_head, offset) ||
      !read_word(cpu, ds, word_after(data_roster_head, 2), segment)) {
    return out;
  }

  for (unsigned walked = 0; walked <= max_roster_walk; ++walked) {
    if (offset == 0 && segment == 0) {
      out.ended = true;
      return out;
    }
    if (segment == 0) {
      // A link that is not a pointer: nothing lives in segment 0, which
      // is the interrupt vector table and the BDA (memory_map.h). The
      // same argument `seam_cheats.cpp` makes about a record on the
      // stack, and the reason a walk that has to end can say it did not.
      return out;
    }
    if (out.members == max_roster_walk) {
      return out;  // longer than a roster can be: not a roster.
    }
    member_reading& who = out.member[out.members];
    ++out.members;
    who.offset = offset;
    who.segment = segment;

    std::uint8_t status = 0;
    if (!read_byte(cpu, segment, word_after(offset, rec_status), status)) {
      return out;
    }
    who.status = status;
    who.heals = heals_by_resting(status);
    // Read for everybody and not only for the members resting can help.
    // The deficit below still turns on the applier's gate — a member it
    // refuses must not lengthen the rest — but the report names them,
    // and a line that could not say where somebody is would be worse
    // than no line.
    if (!read_byte(cpu, segment, word_after(offset, rec_hit_points),
                   who.points) ||
        !read_byte(cpu, segment, word_after(offset, rec_max_hit_points),
                   who.most_points)) {
      return out;
    }
    out.points += who.points;
    if (who.heals && who.most_points > who.points) {
      who.deficit = static_cast<unsigned>(who.most_points - who.points);
      if (who.deficit > out.worst_deficit) {
        out.worst_deficit = who.deficit;
      }
    }

    // Who may cast, and what they are holding. The status gate is the
    // program's own: an animated body does not act, and neither does
    // anyone whose act byte is clear.
    std::uint8_t acts = 0;
    if (!read_byte(cpu, segment, word_after(offset, rec_can_act), acts)) {
      return out;
    }
    who.can_act = acts != 0 && status != status_animated;
    if (who.can_act && knows_a_cure(cpu, segment, offset)) {
      out.anybody_knows_a_cure = true;
    }
    for (unsigned nth = 0; nth < rec_spell_slot_count; ++nth) {
      std::uint8_t slot = 0;
      if (!read_byte(cpu, segment,
                     word_after(offset, static_cast<std::uint16_t>(
                                            rec_spell_slots + nth)),
                     slot)) {
        return out;
      }
      if (slot == 0) {
        if (!who.has_free_slot) {
          who.free_slot = nth;
          who.has_free_slot = true;
        }
      } else if ((slot & spell_pending) != 0) {
        ++out.pending_spells;
        if (is_heal_spell(static_cast<std::uint8_t>(slot & ~spell_pending))) {
          ++who.pending_cures;
          ++out.pending_cures;
        }
      } else if (is_heal_spell(slot)) {
        ++who.ready_cures;
        ++out.ready_cures;
      }
    }

    std::uint16_t next_offset = 0;
    std::uint16_t next_segment = 0;
    if (!read_word(cpu, segment, word_after(offset, rec_next_offset),
                   next_offset) ||
        !read_word(cpu, segment, word_after(offset, rec_next_offset + 2),
                   next_segment)) {
      return out;
    }
    offset = next_offset;
    segment = next_segment;
  }
  return out;
}

/// The days to dial: what the worst-wounded member needs, plus the day of
/// slack the header explains, capped at the program's own clamp.
///
/// **Zero for a party that is whole**, and that is the whole of the fix
/// for a bug this seam shipped with: it used to return at least one, so
/// choosing the Fix with nobody hurt slept a full day for nothing. The
/// day was never the arithmetic's — it was there to keep point 3's old
/// signature non-zero, and point 3 reads a word of this seam's own now.
///
/// Zero does not mean "no rest". It means the rest is the one the
/// program's own wrapper computed — the memorization time — which is
/// exactly the rest the player's own Rest key would have given them.
[[nodiscard]] std::uint16_t days_to_dial(const roster_reading& party) {
  if (party.worst_deficit == 0) {
    return 0;
  }
  const unsigned wanted = party.worst_deficit + 1;
  return static_cast<std::uint16_t>(wanted > max_rest_days ? max_rest_days
                                                           : wanted);
}

/// Whether the BIOS keystroke buffer is empty — head and tail equal, the
/// way the keyboard service's own INT 16h decides it (keyboard.cpp).
///
/// The promise that a key the *player* typed is never overtaken by this
/// seam's: with something already in the buffer, the seam stands aside.
[[nodiscard]] bool keyboard_buffer_empty(cpu::processor& cpu) {
  return cpu.read_word(bda::segment, bda::keyboard_buffer_head) ==
         cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
}

/// Whether this area lets anyone cast at all — the word the program's own
/// cast screen reads before it offers a spell. True when it cannot be
/// read, which is the fail-closed direction: a seam that cast where the
/// program would not have offered to would be changing the rules.
[[nodiscard]] bool casting_allowed(cpu::processor& cpu, std::uint16_t ds) {
  std::uint16_t offset = 0;
  std::uint16_t segment = 0;
  if (!read_word(cpu, ds, data_area_record, offset) ||
      !read_word(cpu, ds, word_after(data_area_record, 2), segment) ||
      segment == 0) {
    return false;
  }
  std::uint16_t refuses = 0;
  if (!read_word(cpu, segment, word_after(offset, area_refuses_casting),
                 refuses)) {
    return false;
  }
  return refuses == 0;
}

// --- The report ------------------------------------------------------------
//
// What the command did, in the game's own font, in the box the program
// frames for its own messages. Everything here is composed in this seam's
// own storage and drawn by the program's own routines (#188): nothing is
// rasterized here, and no word of the program's is carried here — a
// member the command could not help is named in the program's own word
// for their condition, by handing the drawer a pointer into the
// program's own table rather than a copy of what it says.

/// A line of the report: a Pascal string — a length byte and its
/// characters — built here and put on the machine's own stack by
/// `place_bytes()` for exactly as long as the batch that draws it.
///
/// Forty-two bytes because a screen is forty columns and a Pascal string
/// costs one more; the two over are what makes an append that would not
/// fit a clamp rather than an overrun.
class report_line {
 public:
  /// Characters, clamped. A line that ran out of room is a line that
  /// stops, which is what each of these does at the box's edge anyway.
  void add(std::string_view text) {
    for (const char letter : text) {
      if (static_cast<std::size_t>(length_) + 1U >= byte_.size()) {
        return;
      }
      byte_[static_cast<std::size_t>(++length_)] =
          static_cast<std::uint8_t>(letter);
    }
  }

  void add_char(std::uint8_t letter) {
    if (static_cast<std::size_t>(length_) + 1U >= byte_.size()) {
      return;
    }
    byte_[static_cast<std::size_t>(++length_)] = letter;
  }

  /// A number in decimal. Everything the report counts — hit points,
  /// spells, hours, minutes — is a byte or a small word, so five digits
  /// is the whole range and there is no value this cannot print.
  void add_number(unsigned value) {
    std::array<char, 5> digit{};
    std::size_t digits = 0;
    while (digits < digit.size()) {
      digit[digits++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
      if (value == 0) {
        break;
      }
    }
    while (digits != 0) {
      --digits;
      add(std::string_view{&digit[digits], 1});
    }
  }

  /// The same, always two digits — the minutes cell of a clock, where
  /// four minutes past is `04` and not `4`.
  void add_two_digits(unsigned value) {
    if (value < 10) {
      add(std::string_view{"0"});
    }
    add_number(value);
  }

  /// Spaces until the next character would land in `column`, so a line
  /// drawn from `report_name_column` puts its cells where the layout
  /// says. A cell whose contents already overran its column gets one
  /// space instead: columns that do not line up are harder to read, and
  /// two words run together are harder to trust.
  void pad_to(unsigned column) {
    const unsigned wanted = column - report_name_column;
    if (length_ >= wanted) {
      add(std::string_view{" "});
      return;
    }
    while (length_ < wanted) {
      add(std::string_view{" "});
    }
  }

  /// The length byte, written on the way out rather than kept up to date
  /// by every append above.
  void seal() { byte_[0] = length_; }

  [[nodiscard]] std::span<const std::uint8_t> bytes() const {
    return {byte_.data(), static_cast<std::size_t>(length_) + 1U};
  }

 private:
  std::array<std::uint8_t, 0x2A> byte_{};
  std::uint8_t length_{0};
};

/// The title, per outcome — this project's own words for its own
/// command, and the one thing the report says that is not read out of
/// the machine.
[[nodiscard]] constexpr std::string_view title_for(std::uint16_t state) {
  switch (state) {
    case as_word(run_state::healed):
      return "Fix: Party Healed";
    case as_word(run_state::rest_stopped):
      return "Fix: Rest Stopped";
    case as_word(run_state::no_cure_memorized):
      return "Fix: No Cure Memorized";
    case as_word(run_state::nobody_knows_a_cure):
      return "Fix: Nobody Knows Cure";
    case as_word(run_state::cannot_cast_here):
      return "Fix: Cannot Cast Here";
    case as_word(run_state::player_stopped):
      return "Fix: Stopped";
    default:
      return "Fix";
  }
}

/// The game's clock in minutes, from the three words of the area record
/// the program's own status line reads. Zero when it cannot be read,
/// which costs the summary its elapsed clause and nothing else.
[[nodiscard]] unsigned clock_minutes(cpu::processor& cpu, std::uint16_t ds) {
  std::uint16_t offset = 0;
  std::uint16_t segment = 0;
  if (!read_word(cpu, ds, data_area_record, offset) ||
      !read_word(cpu, ds, word_after(data_area_record, 2), segment) ||
      segment == 0) {
    return 0;
  }
  std::uint16_t hours = 0;
  std::uint16_t tens = 0;
  std::uint16_t units = 0;
  if (!read_word(cpu, segment, word_after(offset, area_clock_hours), hours) ||
      !read_word(cpu, segment, word_after(offset, area_clock_minute_tens),
                 tens) ||
      !read_word(cpu, segment, word_after(offset, area_clock_minute_units),
                 units)) {
    return 0;
  }
  return (unsigned{hours} * 60U) + (unsigned{tens} * 10U) + unsigned{units};
}

/// Minutes from `began` to `now`. **The clock wraps at midnight and
/// carries no day counter here**, so a run that crossed it reads
/// backwards; one day added back is the answer, and one wrap is the only
/// case there is — a command that rested a whole day would have stopped
/// when the party came whole long before.
[[nodiscard]] unsigned minutes_since(unsigned began, unsigned now) {
  return now >= began ? now - began : (now + minutes_in_a_day) - began;
}

/// Whether the report names this member: somebody the command could not
/// put right, either because resting cannot help them at all or because
/// they are still short of their maximum.
[[nodiscard]] bool is_an_exception(const member_reading& who) {
  return !who.heals || who.points < who.most_points;
}

/// The summary — the one line only this command can write, because every
/// other number on the screen says where things are *now* and this is a
/// difference against a before the machine has stopped holding.
///
/// A rest restores hit points with no spell spent, so the spell clause is
/// dropped rather than printed as "with 0 spells"; and a command that
/// healed nobody says so rather than printing a zero.
[[nodiscard]] report_line summary_line(unsigned restored, unsigned casts,
                                       unsigned minutes) {
  report_line line;
  if (restored != 0) {
    line.add(std::string_view{"Healed "});
    line.add_number(restored);
    line.add(std::string_view{" HP"});
    if (casts != 0) {
      line.add(std::string_view{" with "});
      line.add_number(casts);
      line.add(casts == 1 ? std::string_view{" spell"}
                          : std::string_view{" spells"});
    }
  } else {
    line.add(std::string_view{"No hit points restored"});
  }
  if (minutes != 0) {
    line.add(std::string_view{" in "});
    line.add_number(minutes / 60U);
    line.add(std::string_view{":"});
    line.add_two_digits(minutes % 60U);
  }
  line.add(std::string_view{"."});
  line.seal();
  return line;
}

/// One call to the program's own string drawer, at a cell. The frame is
/// the fact `docs/seams.md` §3 records: the arguments in the reverse of
/// the order the routine's own callers name them, a far pointer's segment
/// before its offset.
[[nodiscard]] bool draw_string(seam_context& ctx, std::uint16_t image,
                               const report_line& line, std::uint16_t colour,
                               std::uint16_t row, std::uint16_t column) {
  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  if (!ctx.place_bytes(line.bytes(), segment, offset)) {
    return false;
  }
  const std::array<std::uint16_t, 5> where{column, row, colour, segment,
                                           offset};
  return ctx.call_program(image, image_draw_string, where);
}

/// The same, for a string already in the program's memory — the
/// wound-status table. Nothing is placed and nothing is copied: the
/// drawer is handed where the program keeps its own word.
[[nodiscard]] bool draw_program_string(seam_context& ctx, std::uint16_t image,
                                       std::uint16_t segment,
                                       std::uint16_t offset,
                                       std::uint16_t colour, std::uint16_t row,
                                       std::uint16_t column) {
  const std::array<std::uint16_t, 5> where{column, row, colour, segment,
                                           offset};
  return ctx.call_program(image, image_draw_string, where);
}

/// One exception line, up to its reason: the name out of the record, then
/// the hit points. False when a read of the name would land outside
/// conventional memory, which is the refusal every read here makes.
[[nodiscard]] bool exception_line(cpu::processor& cpu,
                                  const member_reading& who,
                                  report_line& line) {
  std::uint8_t name_length = 0;
  if (!read_byte(cpu, who.segment, who.offset, name_length)) {
    return false;
  }
  const unsigned letters =
      name_length > report_name_width ? report_name_width : name_length;
  for (unsigned at = 0; at < letters; ++at) {
    std::uint8_t letter = 0;
    if (!read_byte(cpu, who.segment,
                   word_after(who.offset, static_cast<std::uint16_t>(at + 1)),
                   letter)) {
      return false;
    }
    line.add_char(letter);
  }
  line.pad_to(report_points_column);
  line.add_number(who.points);
  line.add(std::string_view{"/"});
  line.add_number(who.most_points);
  return true;
}

/// Queue the whole report as one batch (§3): the frame with its title,
/// the summary, the members it could not put right, and the warning when
/// there is one.
///
/// **It is one batch and not several arrivals**, because a report is one
/// picture: a handler that drew half of it and was declined the rest
/// would leave a titled box with nothing in it. Twelve calls is the
/// engine's bound and the worst case here is ten — the frame, the
/// summary, and four rows of at most two calls each.
///
/// **There is no pager.** The list truncates to a line saying how many it
/// did not name, which is the proven design's own cut (PLAN.md §5) and
/// removes the one part of a report that can be got wrong. What that
/// costs is less than it looks: the roster panel is still on screen
/// behind the box, with every member's hit points on it.
[[nodiscard]] bool draw_the_report(cpu::processor& cpu, seam_context& ctx,
                                   std::uint16_t ds,
                                   const roster_reading& party,
                                   std::uint16_t state, unsigned restored,
                                   unsigned casts, unsigned minutes) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);

  report_line title;
  title.add(title_for(state));
  title.seal();
  std::uint16_t title_segment = 0;
  std::uint16_t title_offset = 0;
  if (title.bytes().size() > report_title_width + 1U ||
      !ctx.place_bytes(title.bytes(), title_segment, title_offset)) {
    return false;
  }
  // The frame clears the panel as it draws it, so whatever the last
  // driven cast or rest left there goes with it. Its title lands on the
  // box's first interior row rather than on the border, which is why the
  // title row and the box's top are the same number.
  const std::array<std::uint16_t, 8> frame{
      cast_region_left,   cast_region_top,   cast_region_right,
      cast_region_bottom, cast_region_style, report_frame_colour,
      title_segment,      title_offset};
  if (!ctx.call_program(image, image_draw_frame, frame)) {
    return false;
  }

  const report_line summary = summary_line(restored, casts, minutes);
  if (!draw_string(ctx, image, summary, report_body_colour, report_summary_row,
                   report_name_column)) {
    return false;
  }

  unsigned exceptions = 0;
  for (unsigned nth = 0; nth < party.members; ++nth) {
    exceptions += is_an_exception(party.member[nth]) ? 1U : 0U;
  }

  // The warning owns a row when there is one, so the list is that much
  // shorter. A cure queued back and not yet memorized is not a cure the
  // party has, and this is #189's promise saying out loud when it could
  // not quite be kept.
  const bool warn_pending = party.pending_cures != 0;
  unsigned rows = (cast_region_bottom - report_first_row) + 1U;
  if (warn_pending) {
    --rows;
  }
  auto row = static_cast<std::uint16_t>(report_first_row);

  if (exceptions == 0) {
    // Nobody to name. Say what the absent list means, rather than leaving
    // the player to read four blank rows as the answer.
    report_line whole;
    whole.add(std::string_view{"The party is at full hit points."});
    whole.seal();
    if (!draw_string(ctx, image, whole, report_body_colour, row,
                     report_name_column)) {
      return false;
    }
  } else {
    const unsigned cap = exceptions > rows ? rows - 1U : exceptions;
    unsigned shown = 0;
    for (unsigned nth = 0; nth < party.members && shown < cap; ++nth) {
      const member_reading& who = party.member[nth];
      if (!is_an_exception(who)) {
        continue;
      }
      report_line line;
      if (!exception_line(cpu, who, line)) {
        return false;
      }
      if (who.heals) {
        // Still short, and resting could have helped: the shortfall is
        // the whole explanation, and it is a number the player can act on
        // without being told what to do about it.
        line.pad_to(report_reason_column);
        line.add(std::string_view{"short "});
        line.add_number(static_cast<unsigned>(who.most_points - who.points));
        line.seal();
        if (!draw_string(ctx, image, line, report_body_colour, row,
                         report_name_column)) {
          return false;
        }
      } else {
        // No cure can legally reach them, and the status that put them
        // out of reach *is* the explanation — in the program's own word
        // for it, from the program's own table.
        line.seal();
        if (!draw_string(ctx, image, line, report_body_colour, row,
                         report_name_column) ||
            !draw_program_string(
                ctx, image, ds,
                static_cast<std::uint16_t>(data_status_names +
                                           (who.status * status_name_stride)),
                report_warning_colour, row, report_reason_column)) {
          return false;
        }
      }
      row = static_cast<std::uint16_t>(row + 1);
      ++shown;
    }
    if (shown < exceptions) {
      report_line tail;
      tail.add(std::string_view{"...and "});
      tail.add_number(exceptions - shown);
      tail.add(std::string_view{" more."});
      tail.seal();
      if (!draw_string(ctx, image, tail, report_warning_colour, row,
                       report_name_column)) {
        return false;
      }
    }
  }

  if (warn_pending) {
    report_line warning;
    warning.add(std::string_view{"Cures are still being memorized."});
    warning.seal();
    if (!draw_string(ctx, image, warning, report_warning_colour,
                     static_cast<std::uint16_t>(cast_region_bottom),
                     report_name_column)) {
      return false;
    }
  }
  return true;
}

/// Which title a command that never got as far as a rest has earned.
///
/// The order is the order the answers stop being interesting in: a party
/// that is whole is healed however it got that way; then the two reasons
/// nothing could be cast, which are different sentences; and what is left
/// is a party that knows cures and had none ready.
[[nodiscard]] run_state outcome_without_a_rest(cpu::processor& cpu,
                                               std::uint16_t ds,
                                               const roster_reading& party) {
  if (party.worst_deficit == 0) {
    return run_state::healed;
  }
  if (!casting_allowed(cpu, ds)) {
    return run_state::cannot_cast_here;
  }
  if (!party.anybody_knows_a_cure) {
    return run_state::nobody_knows_a_cure;
  }
  return run_state::no_cure_memorized;
}

/// The report, if the command that ran owes one: read the party as it is
/// now, work out the difference against the words kept from before, and
/// queue the whole box (§3).
///
/// **Every state but idle is reported**, and that is deliberate rather
/// than tidy. `resting` is the expected way here — a rest has happened
/// and this is the first instant its result is readable. `running` should
/// not be, because a command under way does not hand the camp menu back;
/// if it is, something took the program out of the command and the player
/// is owed the same account of what was done before it. A state that
/// could reach this point and not be cleared would be a seam trying to
/// draw a box on every pass of the menu for ever, and a command whose
/// before-half could never be taken again.
[[nodiscard]] bool draw_a_report_if_one_is_owed(machine& box, seam_context& ctx,
                                                std::uint16_t ds) {
  const std::uint16_t state = ctx.scratch(scratch_state);
  if (state == as_word(run_state::idle)) {
    return false;
  }
  if (state == as_word(run_state::resting) &&
      ctx.scratch(scratch_rest_is_ours) != 0) {
    // **The rest has not happened yet**, and this arrival is the camp
    // loop going round once between the Rest key point 2 posted and the
    // rest command reading it — the Fix chose a letter the camp loop does
    // not know, so the loop redraws its bar before it sees the key.
    //
    // Reporting here would put a box on the screen saying what a rest
    // that had not happened achieved. The claim point 3 clears when the
    // rest starts is exactly the word that tells the two passes apart,
    // and it is already in the machine; this needs no state of its own.
    //
    // The `tests/programs` stand-in found this, which is what that
    // stand-in is for (§8.3): driven, it would have been a report with
    // every number in it zero.
    return false;
  }
  cpu::processor& cpu = box.processor();
  const roster_reading party = read_roster(cpu, ds);
  if (!party.ended || party.members == 0) {
    // Nothing readable to report on. Drop it rather than draw a box full
    // of numbers nobody can stand behind.
    ctx.set_scratch(scratch_state, as_word(run_state::idle));
    return false;
  }

  std::uint16_t outcome = state;
  if (state == as_word(run_state::resting)) {
    // Whether the rest ran its course is the difference between a party
    // that came out whole and one that did not — read off the party
    // rather than remembered, because the program's own stop-resting
    // question and its own wandering monsters both end a rest without
    // telling anybody.
    outcome = party.worst_deficit == 0 ? as_word(run_state::healed)
                                       : as_word(run_state::rest_stopped);
  } else if (state == as_word(run_state::running)) {
    outcome = as_word(outcome_without_a_rest(cpu, ds, party));
  }

  const unsigned before = ctx.scratch(scratch_points_before);
  const unsigned restored = party.points > before ? party.points - before : 0U;
  // Zero minutes means "say nothing about the time", and a command that
  // dialled days is exactly the case where the clock cannot answer.
  const unsigned minutes =
      ctx.scratch(scratch_days_asked) != 0
          ? 0U
          : minutes_since(ctx.scratch(scratch_clock_before),
                          clock_minutes(cpu, ds));
  const unsigned casts = ctx.scratch(scratch_casts);

  // Cleared before the drawing and not after it. A report that could not
  // be queued is one report lost; a state left set would be a box the
  // seam tried and failed to draw on every pass of the menu from here on.
  ctx.set_scratch(scratch_state, as_word(run_state::idle));
  ctx.set_scratch(scratch_casts, 0);
  return draw_the_report(cpu, ctx, ds, party, outcome, restored, casts,
                         minutes);
}

// --- The points ------------------------------------------------------------

/// Point 1: the bar, on its way to being drawn.
void offer_the_fix(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  auto& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];

  std::uint8_t mode = 0;
  if (!read_byte(cpu, ds, data_game_mode, mode) || mode != mode_camp) {
    // The address says this is the camp loop; the mode byte is what says
    // the machine agrees. A point that fires anywhere else is a fact that
    // has gone wrong, and the fail-closed direction is to draw nothing.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  // A command that has finished, reported before the bar goes out — so
  // the box and the live bar under it arrive on the same screen. **The
  // way out is that bar**: it is the camp screen's own, with the
  // program's own EXIT on it, and any key the player presses takes them
  // off this screen and takes the box with it. That is #186's rule one
  // layer on — the prompt on screen is the prompt that works — and it is
  // why this report needs no bar of its own.
  //
  // The batch re-offers this point when it is done (§3), and by then the
  // state is idle, so the arrival after it splices as usual.
  if (draw_a_report_if_one_is_owed(box, ctx, ds)) {
    return;
  }
  if (!splice_in(cpu, ds)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  // The prompt's own columns, which the longer bar needs. It is a Pascal
  // string on the loop's stack, rebuilt on every pass, so this is undone
  // by the program itself rather than by this seam.
  if (!write_byte(
          cpu, regs[cpu::sreg::ss],
          static_cast<std::uint16_t>(regs[cpu::reg16::bp] - frame_prompt), 0)) {
    ctx.decline(seam_reason::point_not_recognized);
  }
}

/// Spend one ready cure on the worst-wounded member the program's own
/// healing would accept, through the program's own cast driver. False
/// when there is nothing to spend, nobody to spend it on, or nowhere to
/// do it — and then the caller rests instead.
///
/// **The cure is queued back before it is spent, and that is the rule of
/// record** (#189): the party ends holding exactly the cures it started
/// with. Writing the pending slot first is what makes that true at every
/// instant rather than at the end — there is no moment at which the
/// player is a cure down, and this handler therefore needs no memory of
/// what it has spent. What it costs is that a cast the program *refuses*
/// leaves a cure pending rather than ready, which the backstop below
/// bounds and a later report will say out loud.
///
/// Everything else here is the program's: the frame and the clear are
/// what its own cast screen draws first, the sort is what its own
/// memorize command runs after it writes a slot, the picker opens on the
/// member the anchor names and one keystroke finishes the pick, and the
/// roll, the announce, the overheal clamp and the forget are the driver's.
[[nodiscard]] bool cast_one_cure(machine& box, seam_context& ctx,
                                 std::uint16_t ds,
                                 const roster_reading& party) {
  if (ctx.scratch(scratch_casts) >= max_casts ||
      !casting_allowed(box.processor(), ds)) {
    return false;
  }

  const member_reading* target = nullptr;
  const member_reading* caster = nullptr;
  for (unsigned nth = 0; nth < party.members; ++nth) {
    const member_reading& who = party.member[nth];
    if (who.deficit != 0 &&
        (target == nullptr || who.deficit > target->deficit)) {
      target = &who;
    }
    if (caster == nullptr && who.can_act && who.ready_cures != 0) {
      caster = &who;
    }
  }
  if (target == nullptr || caster == nullptr) {
    return false;
  }

  cpu::processor& cpu = box.processor();
  // Queued back first (see above), into the first empty slot, which is
  // where the program's own memorize command puts one.
  if (caster->has_free_slot) {
    cpu.write_byte(
        caster->segment,
        word_after(caster->offset, static_cast<std::uint16_t>(
                                       rec_spell_slots + caster->free_slot)),
        static_cast<std::uint8_t>(heal_spells[0] | spell_pending));
  }

  // Who is casting, and at whom. Both are far pointers the program's own
  // screens write before they hand over.
  cpu.write_word(ds, data_current_member, caster->offset);
  cpu.write_word(ds, word_after(data_current_member, 2), caster->segment);
  cpu.write_word(ds, data_cast_anchor, target->offset);
  cpu.write_word(ds, word_after(data_cast_anchor, 2), target->segment);

  // The key that answers the picker, in the buffer before the driver
  // reads it — the way one a player typed a moment earlier would be.
  if (!ctx.inject_keystroke(pick_key_scancode, pick_key_ascii)) {
    return false;
  }

  // Somewhere for the driver's out-flag to land. This seam does not read
  // it back: whether the spell landed is a thing the machine says, in
  // hit points and in a slot that is no longer holding a cure, and the
  // next arrival reads that rather than a byte on a dead stack.
  std::uint16_t out_segment = 0;
  std::uint16_t out_offset = 0;
  const std::array<std::uint8_t, 2> out_flag{0, 0};
  if (!ctx.place_bytes(out_flag, out_segment, out_offset)) {
    return false;
  }

  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const auto module = static_cast<std::uint16_t>(ctx.module_base() / 16U);

  // In the order the program does them. Arguments are pushed in the
  // reverse of the order each routine's own callers name them, with a far
  // pointer's segment before its offset (`docs/seams.md` §3).
  const std::array<std::uint16_t, 0> nothing{};
  const std::array<std::uint16_t, 5> border{
      cast_region_left, cast_region_top, cast_region_right, cast_region_bottom,
      cast_region_style};
  const std::array<std::uint16_t, 4> clear{
      cast_region_left, cast_region_top, cast_region_right, cast_region_bottom};
  // Announce off, and this is a fact about the driver rather than a
  // preference: its announcement ends in the program's own message delay,
  // which takes a waiting key as "the player has read it" and drains the
  // buffer — so the keystroke meant for the target picker never reaches
  // it and the picker waits for ever. Driven against the program, that is
  // exactly what happened, and the engine's own step budget was what
  // said so (#188).
  const std::array<std::uint16_t, 5> cast{heal_spells[0], 0, 0, out_segment,
                                          out_offset};
  const bool queued =
      ctx.call_program(module,
                       static_cast<std::uint16_t>(module_sort_spell_slots),
                       nothing) &&
      ctx.call_program(image, image_frame_border, border) &&
      ctx.call_program(image, image_clear_region, clear) &&
      ctx.call_program(image, image_cast_driver, cast);
  if (!queued) {
    return false;
  }
  ctx.set_scratch(scratch_casts,
                  static_cast<std::uint16_t>(ctx.scratch(scratch_casts) + 1));
  return true;
}

/// Point 2: the letter that came back, and the bar put back as it was.
void take_the_answer(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  auto& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];

  // Unconditionally, and before anything else can decline: outside the one
  // call that drew it, the program's string is the program's string.
  static_cast<void>(splice_out(cpu, ds));

  std::uint8_t out_flag = 0;
  if (!read_byte(
          cpu, regs[cpu::sreg::ss],
          static_cast<std::uint16_t>(regs[cpu::reg16::bp] - frame_out_flag),
          out_flag) ||
      out_flag != 0 || regs.get(cpu::reg8::al) != fix_key_ascii) {
    // Somebody else's key. If it is neither this seam's letter nor the
    // Rest key this seam posts, it is also the moment to let go of any
    // claim on a rest: the claim is meant to live exactly from the Rest
    // this seam posts to the rest command that reads it, and a key the
    // player typed getting there first would otherwise leave it to be
    // spent on the *player's* next rest.
    //
    // **Rest is excluded, and leaving it out is what broke it once**: the
    // key this seam posts comes back through this very point on the next
    // pass of the menu, so clearing on every non-Fix letter cleared the
    // claim with the seam's own keystroke — and the rest command then
    // found no claim, pressed nothing, and left the player on a rest
    // screen waiting for a key that was never coming.
    if (regs.get(cpu::reg8::al) != rest_key_ascii) {
      ctx.set_scratch(scratch_rest_is_ours, 0);
      // And a command that was under way when somebody else's letter came
      // back off the bar has been stopped, whatever stopped it. Point 1
      // would report it anyway; naming it here is what gets the title
      // right rather than leaving it to be inferred from a roster.
      if (ctx.scratch(scratch_state) == as_word(run_state::running)) {
        ctx.set_scratch(scratch_state, as_word(run_state::player_stopped));
      }
    }
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const roster_reading party = read_roster(cpu, ds);
  if (!party.ended || party.members == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (ctx.scratch(scratch_state) == as_word(run_state::idle)) {
    // The command begins here, and this is the only place the before-half
    // of the report is readable: the party's hit points and the clock, as
    // they are the instant before anything is spent (§8.2).
    ctx.set_scratch(scratch_state, as_word(run_state::running));
    ctx.set_scratch(scratch_points_before,
                    static_cast<std::uint16_t>(party.points));
    ctx.set_scratch(scratch_clock_before,
                    static_cast<std::uint16_t>(clock_minutes(cpu, ds)));
    ctx.set_scratch(scratch_casts, 0);
    ctx.set_scratch(scratch_days_asked, 0);
  }
  if (!keyboard_buffer_empty(cpu)) {
    // **This is where the player stops it.** The Fix decides one act per
    // arrival, and every arrival stands aside if there is a key the
    // program has not read yet — so anything typed during the run ends it
    // here, with whatever healing has already happened kept and the camp
    // menu in front of the player. The granularity is one cast, which is
    // as often as this seam is ever in a position to hand control back.
    //
    // It is also the promise that a key the player typed is never
    // overtaken by one of this seam's: standing aside costs them nothing,
    // and going first would cost them the keystroke.
    //
    // A command that was under way owes a report saying so, and the key
    // the player typed is what draws it: the camp loop goes round, point
    // 1 arrives, and the box says what was done before they stopped it.
    if (ctx.scratch(scratch_state) == as_word(run_state::running)) {
      ctx.set_scratch(scratch_state, as_word(run_state::player_stopped));
    }
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // Any anchor this seam left behind, cleared before it decides anything.
  // The picker seeds it from the current record when it is zero, so
  // leaving one set would be leaving the program a target nobody chose.
  cpu.write_word(ds, data_cast_anchor, 0);
  cpu.write_word(ds, word_after(data_cast_anchor, 2), 0);

  // A cure to spend, if there is one and something to spend it on. This
  // is the one arrival's worth of work; the machine comes back here when
  // the calls are done, and the next arrival decides again from what it
  // then reads.
  if (cast_one_cure(box, ctx, ds, party)) {
    return;
  }

  // Nothing left to cast. Is there anything left to rest *for*?
  //
  // Two things make a rest worth asking for: a hit point somebody is
  // short, and a spell somebody is holding pending, which only time turns
  // into one they can cast. With neither, **the Fix does nothing at all**
  // — and doing nothing is the entire point of this branch. Asking for a
  // rest anyway is what it used to do, and it cost a whole party a day of
  // their game for no reason.
  if (party.worst_deficit == 0 && party.pending_spells == 0) {
    ctx.set_scratch(scratch_state,
                    as_word(outcome_without_a_rest(cpu, ds, party)));
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // The word the rest screen's own daYs-then-Inc writes, written once to
  // where that many presses would have left it. Zero is a real answer: it
  // leaves the duration the program's own wrapper computed, which is the
  // rest the player's own Rest key would have given them.
  const std::uint16_t days = days_to_dial(party);
  cpu.write_word(ds, data_rest_days, days);
  ctx.set_scratch(scratch_days_asked, days);
  // The rest that is about to happen is this seam's, and point 3 reads
  // that here rather than guessing it from the clock.
  ctx.set_scratch(scratch_rest_is_ours, 1);
  // What the rest achieves is only readable after it, so the outcome is
  // not decided here — the camp menu decides it when it comes back.
  ctx.set_scratch(scratch_state, as_word(run_state::resting));
  // And the bar's own Rest key, which is the key a player presses next.
  static_cast<void>(ctx.inject_keystroke(rest_key_scancode, rest_key_ascii));
}

/// Point 3: the rest command, entered because of the key point 2 posted.
void start_the_rest(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];

  static_cast<void>(ds);
  if (ctx.scratch(scratch_rest_is_ours) == 0) {
    // A rest the player asked for themselves. This seam posted no Rest
    // key, so it has nothing to say about the screen that is coming.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  // Consumed here whatever happens next: one posted Rest, one rest
  // screen started, and never a second one on a later rest the player
  // asked for.
  ctx.set_scratch(scratch_rest_is_ours, 0);
  if (!keyboard_buffer_empty(cpu)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  static_cast<void>(ctx.inject_keystroke(rest_key_scancode, rest_key_ascii));
}

/// Three points, all with addresses, all in the camp screen's overlay and
/// all resolved through the program's own note of where that overlay is.
constexpr std::array<seam_point, 3> encamp_fix_points{
    {{.module = camp_module,
      .offset = camp_menu_before_input,
      .run = &offer_the_fix},
     {.module = camp_module,
      .offset = camp_menu_after_input,
      .run = &take_the_answer},
     {.module = camp_module,
      .offset = rest_command_entry,
      .run = &start_the_rest}}};

constexpr seam_definition encamp_fix_definition{
    .id = "encamp-fix",
    .about = "put Fix on the camp menu: rest as long as the party needs",
    .fingerprints = encamp_binaries,
    .points = encamp_fix_points,
    // Not a trigger (#186). It was one, because a pull was the only way to
    // ask; the asking is now a key on the game's own bar, and a seam that
    // needed a pull *as well* would be asking twice. What "on" means for
    // this seam is that the command is offered — the same thing "on" means
    // for the code-wheel seam, which answers a challenge whenever the
    // challenge is asked.
    .trigger = false,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& encamp_fix_seam() noexcept {
  return encamp_fix_definition;
}

}  // namespace amberfolio::machine
