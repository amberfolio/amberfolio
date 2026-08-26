// SPDX-License-Identifier: AGPL-3.0-only
//
// The Encamp (F)ix: PLAN.md §5 item 4, M5-E1 (#172), and the first
// enhancement of M5 because #165 found it needs no door — every primitive
// it uses was already built and tested.
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
//
// What the program does, stated as facts
// --------------------------------------
//
// Addresses and offsets, which the clean-content rule allows and
// CONTRIBUTING.md names explicitly. Not a byte of the program is
// reproduced here and nothing here describes its screens.
//
// **Camp is a mode.** The data segment's game-mode byte reads 2 while the
// camp screen is up (`docs/playable.md` records the same byte and the
// same value, found by watching a run). The camp menu offers Rest among
// its commands, and choosing it runs a wrapper that:
//
//   * walks the party roster — the same far-linked list the debug cheats
//     walk, off the same head word — taking the longest spell
//     memorization time any member needs, and
//   * decomposes that into the rest clock's own fields before the rest
//     screen is drawn, so the duration a player is offered already covers
//     memorization.
//
// **The rest clock is seven words in the data segment**, of which four
// are the ones the rest screen shows: minute units, minute tens, hours
// and days, at the offsets named below. The camp screen zeroes the whole
// block on entry and the rest wrapper zeroes it again afterwards, so a
// rest always begins from what that wrapper computed. The rest screen's
// own Inc key increments one of these words and normalizes the carries;
// **the program clamps the days field at 99**, which is where
// `max_rest_days` comes from — it is the program's number, not one this
// file chose.
//
// **The rest screen waits for a key.** It is a menu bar like every other:
// it draws the clock, asks for a key, and acts on it — Rest starts the
// rest, Exit leaves, the field keys pick which word Inc and Dec move.
// Enter means Rest. Nothing happens until a key arrives.
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
//     iteration, from a block the rest zeroes before it starts. That is
//     the guard's clock (below).
//
// The rest ends when the clock runs out, when the player stops it, or
// when a wandering monster interrupts it — the last of which takes the
// party out of camp entirely, which is exactly what #172 asks this seam
// to notice.
//
//
// What the seam does
// ------------------
//
// One point, no address (`seam_point::at_every_step`), offered at every
// step boundary while a pull is outstanding. When its guard holds — the
// party is in camp, the rest screen is up, and the rest has not started —
// it does two things and stops:
//
//   1. **writes the days field**, through the bus, with the number of
//      days the party needs to be whole: the largest hit-point deficit
//      over the members the program's own heal applier would accept, plus
//      one, capped at the program's own 99. It is the word the rest
//      screen's own Inc key writes, and a player pressing daYs and then
//      Inc that many times writes the same word to the same value;
//   2. **posts the Rest key** into the BIOS keystroke buffer, which is
//      what a player pressing R does.
//
// Then the program rests. Time passes on the game's calendar, pending
// spells are memorized at the game's own rate, hit points come back one a
// day through the game's own applier, and the game's own wandering
// monsters roll against the game's own odds. Nothing else is touched.
//
// **The plus one is not a fudge factor.** The heal tick counts iterations
// in a counter the camp screen zeroes on entry and the rest does *not*
// reset between rests, so a second rest in one camp session starts
// part-way through a day and would come up one hit point short. A day of
// slack costs the player nothing they did not ask for — they asked to be
// whole — and removes the one way this arithmetic can be wrong.
//
//
// Why it is one act and not a loop, which is what #172 asked for
// --------------------------------------------------------------
//
// #172 describes an orchestration: memorize, rest, heal, look again. Two
// facts about this machine and this program turn that loop into a single
// act, and both are worth writing down because they will shape every seam
// that drives the program through its own menus.
//
// **The program drains the keyboard after every key it reads.** Its
// keystroke routine, having read one key, reads and throws away whatever
// else is in the buffer. So a handler that posts two keys posts one: the
// screen that asked consumes the last and the rest is gone. One key per
// arrival is the only rate that works, and a driver therefore needs one
// handler call per key.
//
// **A pull is a one-shot latch** (seam.h, #161): the handler runs on the
// arrival that serves the pull and not again. So a pulled seam gets one
// act, and a multi-key driver would have to be a seam that runs at every
// arrival — which is a setting, not a command, and would rest the party
// every time they camped to save the game.
//
// One act is enough because **the party heals in parallel**: every member
// gains a hit point on every rest day, so the days the worst-wounded
// member needs are the days everybody needs. The loop in the later
// titles' FIX exists because those cast cure spells between rests, and
// this one does not (below). What one act cannot do is *look again* after
// an interruption — and an interruption ends camp, which ends the Fix
// anyway. The player pulls again.
//
//
// What a later Gold Box title's FIX did that this one does not
// ------------------------------------------------------------
//
// #172 asks for this list at the point of definition, and it is short:
//
//   * **it memorized cure spells for you**, filling empty slots with
//     cures so the rests were spent on healing magic;
//   * **it cast them** afterwards, through the game's own cast driver,
//     and looped until the party was whole;
//   * and in at least one title it **made room** by forgetting ready
//     spells that were not cures.
//
// The first two are out of reach of one act rather than out of taste. A
// seam that memorized cures would have to put the player's own loadout
// back afterwards, which is a second act after the rest, and there is no
// second act. Leaving somebody's memorized spells replaced by cures
// because the mechanism could not undo it is precisely the kind of thing
// PLAN.md §5's native-feel rule refuses. The third one this project would
// refuse anyway, for the reason the earlier implementation gave: the game
// has no by-hand forget, so a Fix that forgot spells would be changing
// the rules rather than saving keystrokes.
//
// What the player keeps is the half that costs nothing: whatever they
// queued for memorization before pressing Rest is memorized during the
// rest this seam pays for, by the program, on the program's own clock.
//
//
// What it is not yet
// ------------------
//
// **The ask is a pull, not a key at the camp menu.** #172 allows either
// and asks for the choice to be recorded: the automap's hotkey claim
// (#173) is the mechanism a key would need — a point inside the program's
// own input funnel, able to consume a keystroke the program has not read
// yet — and #173 has not landed. A pull needs nothing new, is what the
// kill-all cheat already uses, is recorded by a replay as an event with a
// tick, and does not change the number of input polls the program makes.
// When #173 lands, the F this enhancement is named after is one more
// point on this seam and nothing else changes.
//
// **This seam has no address**, which is unusual and is a cost. Its facts
// are data-segment offsets and record offsets — the same table the debug
// cheats are written from, and every one of them either already appears
// in this tree or was found the same way. What it does not have is a code
// address: the rest screen lives in an overlaid module, and a point there
// wants the word the overlay manager keeps that module's segment in
// (#131), which nobody in this tree has located for that module. So the
// point buys its safety from the machine instead, the way the kill-all
// cheat's immediate point does, and `camp_rest_ready()` below says what
// that guard can and cannot rule out. Finding that word is what a later
// change would do to make this a point with an address; the method is in
// `seam_cheats.cpp`.
//
// **Nobody has watched a hit point come back on a driven run yet.** The
// mechanism has a public test (`tests/core/machine/seam_encamp_test.cpp`
// drives the handler over a roster the test writes, and
// `tests/programs`' camp stand-in drives the same shape through the whole
// machine on all four targets). The arithmetic — one hit point per member
// per day — is a fact read off the program's own rest loop and not a
// measurement. `docs/seams.md` §10 says which of the two each claim is.

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

// --- The program's data segment --------------------------------------------
//
// Offsets in the data segment DS addresses throughout the program. The
// first two are already in this tree (`seam_cheats.cpp`, and the mode
// byte is `docs/playable.md`'s `49F3`); the rest are the rest screen's.

/// The game mode byte, and the value it reads while the camp screen is up.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_camp = 2;

/// The roster list's head: a far pointer, offset then segment. The party,
/// which is the list the rest's heal tick walks.
constexpr std::uint16_t data_roster_head = 0x5D96;

/// The days field of the rest clock — the one word this seam writes.
///
/// The clock is seven words beginning at 0x6DC2, of which four are what
/// the rest screen shows and the rest loop counts down: minute units at
/// +2, minute tens at +4, hours at +6, and days at +8. Only the last is
/// named as a constant because only the last is touched; the other three
/// are the program's own arithmetic, filled by the rest wrapper from the
/// party's memorization time and left exactly as they were found.
constexpr std::uint16_t data_rest_days = 0x6DC2 + 8;

/// The per-member memorization countdown: one byte per roster member,
/// **one-based** — the first member's is at `data_memorize_countdown`,
/// the second's one byte later. The rest zeroes every one of them before
/// it begins and decrements every one of them on every iteration, which
/// is the only reason this file knows one byte of it.
constexpr std::uint16_t data_memorize_countdown = 0x6DD0;

/// Non-zero while the rest screen is the screen (the program's own
/// layout flag: a message box drawn while it is up goes one row lower).
/// Set as the rest is entered, cleared as it is left.
constexpr std::uint16_t data_rest_screen_up = 0x6DDA;

/// What the program clamps the days field to when its own Inc key
/// normalizes the clock. **Its number, not this file's**, which is what
/// makes it the right backstop: dialling past it would be dialling
/// something the program would not have let a player dial, and the rest
/// screen's two-digit display could not show it either.
constexpr std::uint16_t max_rest_days = 99;

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

/// The key the rest screen's menu bar takes for Rest, as INT 16h hands it
/// back: the scan code for R and its character. The program uppercases
/// what it reads before matching it against the bar, so this is the same
/// key a player types.
constexpr std::uint8_t rest_key_scancode = 0x13;
constexpr std::uint8_t rest_key_ascii = 'R';

// --- Reading the party -----------------------------------------------------

[[nodiscard]] std::uint16_t word_after(std::uint16_t at,
                                       std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(at + by);
}

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
};

/// Whether `segment:offset` is inside conventional RAM — 00000-9FFFF, the
/// memory a program's own structures live in (memory_map.h) — and so
/// whether this seam is willing to read it at all.
///
/// **Checked before every read this file makes, and that is not
/// belt-and-braces.** A point with no address is offered at every step
/// boundary while a pull is outstanding, which means this guard runs with
/// DS holding whatever the program happens to have loaded at that
/// instant — and a read through the bus is a bus cycle. Above conventional
/// memory it is the video window, where a read loads the adapter's
/// latches (ega.h): a guard that perturbed the machine it is inspecting
/// would be doing the one thing a seam may never do.
///
/// It is also how this was found, which is the reason it is written down
/// rather than assumed. Driven against the program, this seam declined
/// its way from the pull to the camp screen and left seven
/// `unmapped_memory_read` notices behind it — a roster walk following a
/// far pointer out of a data segment that was not the program's. Nothing
/// was corrupted and nothing was faked; the machine said, correctly, that
/// something had touched memory nobody answers for, and the something was
/// this seam.
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
    ++out.members;

    std::uint8_t status = 0;
    if (!read_byte(cpu, segment, word_after(offset, rec_status), status)) {
      return out;
    }
    if (heals_by_resting(status)) {
      std::uint8_t current = 0;
      std::uint8_t most = 0;
      if (!read_byte(cpu, segment, word_after(offset, rec_hit_points),
                     current) ||
          !read_byte(cpu, segment, word_after(offset, rec_max_hit_points),
                     most)) {
        return out;
      }
      if (most > current) {
        const auto deficit = static_cast<unsigned>(most - current);
        if (deficit > out.worst_deficit) {
          out.worst_deficit = deficit;
        }
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

/// Whether the BIOS keystroke buffer is empty — head and tail equal, the
/// way the keyboard service's own INT 16h decides it (keyboard.cpp).
///
/// Two things at once, and both are needed. It is the closest this seam
/// can get to "the program is waiting for a key rather than doing
/// something with one", and it is the promise that a key the *player*
/// typed is never overtaken by this seam's: with something already in the
/// buffer, the seam stands aside and the pull waits.
[[nodiscard]] bool keyboard_buffer_empty(cpu::processor& cpu) {
  return cpu.read_word(bda::segment, bda::keyboard_buffer_head) ==
         cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
}

/// Whether the machine, at this arbitrary step boundary, is the rest
/// screen waiting for a key — the guard this point has instead of an
/// address (#163, and `seam_cheats.cpp`'s `combat_roster_ready()` is the
/// worked example this follows).
///
/// Read-only, cheapest first, and every condition is a fact this file
/// already knew:
///
///   1. the game mode byte reads camp;
///   2. the program's own rest-screen flag is set, so the rest command is
///      the thing that is running and not some other camp screen;
///   3. the days field is zero — the rest wrapper fills hours and minutes
///      and never days, so a non-zero days field is a duration somebody
///      else dialled, and a seam that overwrote it would be taking a
///      choice away from the player who made it;
///   4. every member's memorization countdown is zero, which the rest
///      sets up immediately before the screen is drawn and destroys on
///      its first iteration — this is what says the rest has **not
///      started**;
///   5. the roster is a roster: far pointers throughout, ending within a
///      walk, with at least one member on it;
///   6. and nothing is in the keystroke buffer.
///
/// **What it cannot rule out.** Conditions 3 and 4 are the ones carrying
/// "the rest has not started", and 4 is not eternal: the countdown bytes
/// wrap down through a byte and can be re-seeded to zero for a member
/// with nothing to memorize, so a party in which *nobody* is memorizing
/// anything passes through a single iteration every 255 in which they are
/// all zero again. If a pull is served in one of those iterations, and
/// the days field happens to be zero at that moment too, this seam will
/// dial days into a rest that is already running and press Rest — and the
/// program, which treats a key arriving during a rest as a request to
/// stop, will ask the player whether to stop resting. That is a question
/// the player answers, not a corruption: every word this seam writes is a
/// word the rest screen's own keys write, and it writes none of them
/// twice. The way to close that window for good is an address, and the
/// header says where the address is and what it would cost.
[[nodiscard]] bool camp_rest_ready(cpu::processor& cpu, std::uint16_t ds,
                                   roster_reading& party) {
  // The three data-segment bytes first, and the roster only once they
  // hold. Order is not style here: the roster walk is the only thing in
  // this file that follows a pointer it read out of memory, and following
  // one out of a data segment that is not the program's is what
  // `in_conventional_ram` above was written for. A step at which the mode
  // byte does not read camp costs this seam one byte read and nothing
  // else.
  std::uint8_t mode = 0;
  if (!read_byte(cpu, ds, data_game_mode, mode) || mode != mode_camp) {
    return false;
  }
  std::uint8_t rest_screen = 0;
  if (!read_byte(cpu, ds, data_rest_screen_up, rest_screen) ||
      rest_screen == 0) {
    return false;
  }
  std::uint16_t days = 0;
  if (!read_word(cpu, ds, data_rest_days, days) || days != 0) {
    return false;
  }

  party = read_roster(cpu, ds);
  if (!party.ended || party.members == 0) {
    return false;
  }
  for (unsigned nth = 0; nth < party.members; ++nth) {
    std::uint8_t countdown = 0;
    if (!read_byte(cpu, ds,
                   word_after(data_memorize_countdown,
                              static_cast<std::uint16_t>(nth)),
                   countdown) ||
        countdown != 0) {
      return false;
    }
  }
  return keyboard_buffer_empty(cpu);
}

/// The days to dial: what the worst-wounded member needs, plus the day of
/// slack the header explains, capped at the program's own clamp. Zero
/// when the party is whole — the rest is then whatever the program's own
/// memorization time made it, which is the rest a player pressing R would
/// have got.
[[nodiscard]] std::uint16_t days_to_dial(const roster_reading& party) {
  if (party.worst_deficit == 0) {
    return 0;
  }
  const unsigned wanted = party.worst_deficit + 1;
  return static_cast<std::uint16_t>(wanted > max_rest_days ? max_rest_days
                                                           : wanted);
}

/// The point (#163): offered at every step boundary while the pull is
/// outstanding, acting at the first one where the guard holds and
/// declining — which keeps the latch — at every one before it.
void rest_until_whole(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  roster_reading party;
  if (!camp_rest_ready(cpu, ds, party)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint16_t days = days_to_dial(party);
  if (days != 0) {
    // The word the rest screen's own daYs-then-Inc writes, written once
    // to where that many presses would have left it. The carry-normalize
    // those presses run afterwards has nothing to do here: days is the
    // top field and everything below it is what the program's own rest
    // wrapper just put there.
    cpu.write_word(ds, data_rest_days, days);
  }
  // And the key that starts it. If the buffer refuses it the rest simply
  // waits for the player, which is the state the screen was already in.
  static_cast<void>(ctx.inject_keystroke(rest_key_scancode, rest_key_ascii));
}

/// One point, and it has no address: it is offered at every step boundary
/// while a pull is outstanding and buys its safety from the guard above.
/// Qualified by the resident image, which is always resident — the
/// module qualifier a point in an overlay would carry is exactly what
/// this seam does not have, and the header says so.
constexpr std::array<seam_point, 1> encamp_fix_points{
    {{.module = resident_image,
      .run = &rest_until_whole,
      .at_every_step = true}}};

constexpr seam_definition encamp_fix_definition{
    .id = "encamp-fix",
    .about =
        "when you pull it at camp, rest as long as the party needs to be "
        "whole",
    .fingerprints = encamp_binaries,
    .points = encamp_fix_points,
    // Pulled, not left on (#161). Camping is something a player does for
    // several reasons — to save, to look at a character, to memorize one
    // spell — and a seam that rested the party every time it saw a camp
    // screen would be a setting rather than a command. The `about` says
    // *at camp* because that is where the pull is served: pulled
    // anywhere else it waits, and a host's listing says it is waiting.
    .trigger = true,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& encamp_fix_seam() noexcept {
  return encamp_fix_definition;
}

}  // namespace amberfolio::machine
