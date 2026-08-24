// SPDX-License-Identifier: AGPL-3.0-only
//
// The debug cheats: invulnerability and kill-all-enemies (PLAN.md §5 item
// 6; M4-F5, #99). The first seams keyed to a real fingerprint, qualified
// by a real overlay, toggled from both hosts — and the ones the
// playthrough sweep (#101) turns on to finish a combat without luck.
//
// Two seams, not one with two switches. PLAN.md §5's first requirement is
// "every seam has its own config key and UI toggle", and the two cheats
// are wanted separately: a sweep that wants combats *won* does not want
// the party unable to lose hit points in the saved game it then writes,
// and a playtest of the damage messages wants the party standing without
// combats ending themselves. Two ids, two switches, recorded in
// docs/seams.md.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// Everything below is addresses, offsets and a digest — facts about a
// binary, which the clean-content rule allows and CONTRIBUTING.md names
// explicitly. Not a byte of the program is reproduced here.
//
// A character is a record, and the record carries — at the offsets named
// below — its current hit points, a wound status (6 is slain), a flag
// that is non-zero while the combatant is up and counted, a combat-side
// index (0 for the party, otherwise the enemies), a far pointer to a
// per-combat scratch block, and a far pointer to the next record in the
// roster list. The list's head is a far pointer in the program's data
// segment, which DS addresses throughout; the game mode byte there reads
// 5 during tactical combat; and a byte per combat side counts the bodies
// still standing, indexed by the side.
//
// **Damage lands in one resident routine.** Whatever deals it — a melee
// result, a spell, a monster's special attack — arrives at one procedure
// in the resident image, reached by a far call, with two arguments: the
// record as a far pointer and the damage as a word. At its entry the
// stack holds the far return address, then **the damage word, then the
// record's offset and segment** — the far pointer pushed first, segment
// then offset, and the damage pushed last. The routine subtracts, sets
// the wound status by how far below zero the record went, writes the
// remaining hit points back if the character is still standing, and
// otherwise downs them: clears the flag, zeroes the hit points,
// decrements the side's body count, and clears a byte in the scratch
// block.
//
// That order is the one thing here that was written down backwards and
// stayed that way through #99, because nothing had ever run it against
// the program (#103). It was found by observation and not by reading the
// program: watch the byte a party member's hit points live at, take the
// address of whatever writes it, and walk the trace ring back to where
// control entered that code. `docs/playable.md` has the method.
//
// **Combat ends in one overlaid routine.** Once a round, the tactical
// loop calls an end check that lives in an overlaid code module; it
// consults each side's body count and answers "over" when one of them has
// reached zero. The overlay manager loads that module with one read —
// `overlay_module` below is the file, the offset and the length of that
// read, and the SHA-256 of the bytes it delivers, as the tracker records
// it (overlay.h).
//
// Which module that is comes from the overlay file's **own table**, and
// not from a trace. #129 asked instead "which recorded load last covered
// the address the end check ran at", got a different overlay entirely,
// and put its numbers here; #130 put the original ones back. Overlays
// share an arena and their landing ranges overlap over a run, so "which
// read covered this address" is not the same question as "which module
// owns this code" and does not have the same answer.
//
// **And where it is comes from the program, not from the read.** The
// manager moves a resident module inside its arena without reading it
// again, so the address the read landed at is not the address the
// routine runs from; that is #131, and `overlay_load_segment_at` below
// is the answer to it — the word the manager itself keeps the module's
// current segment in.
//
//
// What the seams do, and what they are careful not to do
// -----------------------------------------------------
//
// **Invulnerability** intercepts the damage routine's entry and, when the
// record on the stack is a party member's, writes a zero over the damage
// word. The program's own routine then runs on zero damage and reaches
// its own conclusion — the remaining hit points are the hit points, the
// status stays what it was — through its own code. Nothing about the
// record is touched directly, and an enemy's damage is left entirely
// alone: the qualifier is the side index, read from the record the
// program itself is about to read.
//
// **Kill-all-enemies is pulled** (#161). It is a `trigger` seam: while
// it is on, nothing happens, and it acts once for each time a person
// asks — a key on the desktop host, a button on the page, a `pull` line
// in a recording. Before that it fired at every end check, so switching
// it on once decided every subsequent fight, which is a setting rather
// than a cheat.
//
// **And it acts at the moment of the pull** (#163), which #161 did not.
// Its only point used to be the once-a-round end check, so a pull made
// mid-round was served when the round ended; the complaint that made it
// a trigger asked for both halves and got one. It now has two points:
//
//   * a point with **no address** (`seam_point::at_every_step`), offered
//     at every step boundary while the pull is outstanding, which acts
//     at the first step where its guard holds; and
//   * the end check, kept exactly as it was.
//
// The second is not redundant, and keeping it is the reason this change
// is safe to make from a tree with no copy of the program in it. The
// end check is the one address here that has ever been driven against
// the real thing and seen to end a real fight; the guard below is
// reasoning about structures nobody has watched at an arbitrary step.
// If that reasoning is wrong the guard declines, says so once, and the
// pull is served at the end of the round exactly as it was — a slower
// answer rather than a wrong one.
//
// **What the address bought, and what the guard has to buy back.** A
// seam acts at a CS:IP breakpoint because a known instruction in known
// code is a place where the structures the handler edits are known not
// to be half-way through being edited by the program (PLAN.md §5). The
// end check has that argument in the strongest form available here: the
// program's very next act there is to rebuild both sides' counts from
// the roster. A point with no address has no such argument and must
// build one out of what the machine says, which is weaker evidence and
// is written down as such at `combat_roster_ready()` below — including
// the thing it cannot rule out.
//
// **Both points do the same thing**: deal `debug_damage` to every
// standing non-party combatant, exactly as the program's own damage
// routine deals damage to one. A combatant it does not finish keeps its
// remaining hit points and stays standing, in its side's count, with no
// status change; one it does finish is downed the way the routine downs
// one — slain, held flag cleared, hit points zero, the side's body count
// decremented, the scratch byte cleared. Damage rather than a written
// corpse is what makes the mid-round point defensible at all: what this
// leaves behind is a machine state the program produces for itself
// several times a round.
//
// The end check then rebuilds both counts from the held flags — which is
// why the cleared flag and not the decrement is what ends the combat —
// finds the enemies' side at zero, and ends it through its own logic,
// with its own post-combat code running unmodified. Outside combat both
// points decline, and a roster walk is bounded so a record whose next
// pointer is not what the facts say cannot spin the host.
//
// Driven through a wilderness encounter against seven soldiers, one
// firing ends it: `THE PARTY HAS WON. EACH CHARACTER RECEIVES 107
// EXPERIENCE POINTS.` The same script with the seam off is still in the
// fight when the run's tick budget expires. That run predates the
// trigger and was driven by the flag alone; the same script now needs a
// pull as well, which is a line in `docs/playable.md` rather than a
// change to anything committed — no recording in `tests/sessions/`
// enables this seam. It also predates the damage, and what it would
// show now is what nobody in this tree can check: whether 120 finishes
// those seven in one pull, and whether the immediate point serves it
// before the round ends.
//
// Both are fail-closed by construction (#99): unavailable on any binary
// but the baseline's (the fingerprint), inert while the end check's
// module is not loaded — which the program's own record answers at the
// step it is asked, for both of kill-all's points (#97, #131, #163) —
// and nothing on the hot path when off (#96). With kill-all on and
// nobody having pulled it, the address-free point costs one bool and
// the machine is the machine it would have been.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about — the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> cheat_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The character record ----------------------------------------------

/// Offsets into a character record.
constexpr std::uint16_t rec_next_offset = 0x104;     // far ptr: next record
constexpr std::uint16_t rec_scratch_offset = 0x108;  // far ptr: combat scratch
constexpr std::uint16_t rec_status = 0x10C;          // wound status
/// The held flag: whether this record is in its side's body count.
///
/// **Not** a liveness flag, whatever it looks like: the routine that
/// downs a combatant clears it, but a combatant is not up because it is
/// set — `still_standing()` below is that test. What it *is* is the
/// program's own ledger entry for the count: each side's count is
/// **rebuilt** from these bytes, by a walk of this same roster, at the
/// top of every round *and* inside the end check itself, immediately
/// before the count is read. Between those rebuilds the program keeps
/// the count by hand, decrementing as bodies drop.
///
/// So clearing this is what actually ends a combat, and the decrement
/// that goes with it is faithfulness rather than mechanism (#99).
constexpr std::uint16_t rec_held = 0x10D;
constexpr std::uint16_t rec_side = 0x10E;  // combat side, signed

/// The current hit points, and **a byte** — which is a claim worth
/// stating out loud, because until #163 nothing in this tree depended on
/// it. The only write this file ever made here was a zero, and a zero
/// written as a byte is indistinguishable from a zero written as a word
/// whenever the byte above it is already zero, so a field that was
/// really wider would have gone unnoticed. Dealing damage writes a
/// non-zero remainder and does depend on it.
///
/// What is known: the fact table describes a record of single-byte
/// fields — status, held, side — and this one is read and written as a
/// byte here and has been since #99. What is *not* known is that a
/// record whose hit points exceed 255 could not exist; nobody has
/// watched one. The failure that would follow is bounded in the one
/// direction that matters, and `debug_damage` says how.
constexpr std::uint16_t rec_hit_points = 0x11B;

/// The wound status that means slain.
constexpr std::uint8_t status_slain = 6;

/// Whether a combatant is still up, by its wound status: the program's
/// own test, which is membership of a two-element set — unhurt, or an
/// animated body. Everything else is out of the fight.
///
/// This is the fact that matters most in this file and the one easiest
/// to get wrong, because the record has a *held* byte right next to the
/// status that reads like a liveness flag and is not. Testing that byte
/// instead would let this seam down a combatant that is already down —
/// and downing decrements a side's body count, so doing it twice
/// corrupts the count the program ends the combat on.
[[nodiscard]] constexpr bool still_standing(std::uint8_t status) noexcept {
  return status == 0 /* unhurt */ || status == 1 /* animated */;
}

/// The side index that is the party's. Signed, because the record's byte
/// is the signed index the program uses into the body counts.
constexpr std::int8_t side_party = 0;

/// The byte of the combat scratch block the program clears when a
/// combatant is downed.
constexpr std::uint16_t scratch_downed = 3;

/// How much damage a pull deals to each standing enemy.
///
/// **A chosen debug value, not a fact about the program.** Nothing in
/// this file's fact table says what anything in this game hits for; 120
/// is a number somebody picked for a debug cheat, high enough to finish
/// anything a party is likely to meet in one pull and low enough that a
/// combatant who is genuinely tougher survives and is *seen* to survive,
/// which is the difference between a cheat and a corpse-writer.
///
/// Subtracted saturating: 0 is the floor and it never wraps. That is
/// what makes the byte question above survivable rather than dangerous.
/// A saturating subtraction of the low byte can only *lower* the value
/// it is part of — if the field were secretly wider, the byte above it
/// is left alone and the number it holds goes down, never up. The worst
/// this can do to a record is deal less damage than it meant to, or
/// finish a combatant it should not have; it cannot heal one, and it is
/// never applied to the party.
constexpr std::uint8_t debug_damage = 120;

// --- The program's data segment --------------------------------------------
//
// Offsets in the data segment DS addresses throughout the program.

/// The game mode byte, and the value it reads during tactical combat.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_combat = 5;

/// The roster list's head: a far pointer, offset then segment.
constexpr std::uint16_t data_roster_head = 0x5D96;

/// One byte per combat side: how many are still standing.
constexpr std::uint16_t data_side_counts = 0x6814;

/// How many records a roster walk will follow before giving up — far
/// more than the party and a combat's worth of enemies together, and
/// what keeps a record whose next pointer is not what the facts say from
/// spinning the host.
constexpr unsigned max_roster_walk = 64;

// --- Where damage lands ----------------------------------------------------

/// The damage routine's entry, as an offset from the image segment. In
/// the resident image, so the point is qualified by `resident_image`.
constexpr std::uint32_t damage_routine_offset = 0x2DC8;

/// The stack at that entry, as offsets from SP: the far return address
/// (four bytes, because the call is a far one), then the damage word,
/// then the record's far pointer — its offset, then its segment.
///
/// Observed, not assumed. One firing of the point, mid-encounter, with a
/// party member on eight hit points and one enemy on four:
///
///     +0=0489 +2=256A   the far return address
///     +4=0004           the damage: the party member went 8 -> 4
///     +6=0000 +8=3E0A   the record, and the roster says 3E0A:0000 is
///                       the party member
///
/// and the next firing carried `+4=0002 +8=3DF8`, which is the enemy
/// going 4 -> 2. Two arguments, both identified twice, in a frame that
/// reads as itself.
constexpr std::uint16_t frame_damage = 4;
constexpr std::uint16_t frame_record_offset = 6;
constexpr std::uint16_t frame_record_segment = 8;

// --- Where combat ends -----------------------------------------------------

/// Where the program keeps this module's load segment: the offset, in
/// the resident image, of one word (overlay.h,
/// `seam_module::load_segment_at`). It reads zero while the module is
/// not loaded and the segment the module currently begins at while it
/// is, and the overlay manager maintains it — including when it moves
/// the module, which is the whole reason this field exists (#131).
///
/// It sits inside the manager's own record for this module, which
/// begins sixteen bytes earlier at image offset 0x350 and carries, in
/// order, the offset and length above. That is how this offset was
/// found and how it is checked: search the resident image for the
/// record whose file offset is 38919 and whose length is 4735 — one
/// match — and take the word sixteen bytes past its start.
constexpr std::uint32_t overlay_load_segment_at = 0x360;

/// The module the end check lives in: one read, whose facts the tracker
/// records (overlay.h), plus the word above. The digest is of those
/// bytes as read, so a copy whose overlay file does not match is not
/// this module.
///
/// Both qualifiers, because they answer different questions. The read's
/// facts say *which* module this is, and are what a person checks
/// against the overlay file's own table. The word says *where it is
/// now*, which the read cannot: driven through a wilderness encounter on
/// a player-supplied copy, this module was read once and landed at
/// 279D:0000, and was running from 279E:0000 nineteen frames later —
/// and in a later fight it sat 0x73 paragraphs from where its read had
/// put it. The engine arms against the word and never against the
/// landing.
constexpr seam_module overlay_module{
    .file = "GAME.OVR",
    .file_offset = 38919,
    .length = 4735,
    .digest =
        "5d07a6b3fedb56509214f24bdbdbc3b8625ddf6b2ce4d6274e6e89b26c563930",
    .load_segment_at = overlay_load_segment_at};

/// The end check's entry, as an offset from the start of the module —
/// which is to say from the segment the word above holds, wherever
/// the manager has most recently put it.
constexpr std::uint32_t end_check_offset = 0x0880;

// --- The handlers ------------------------------------------------------------

[[nodiscard]] std::uint16_t word_after(std::uint16_t at,
                                       std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(at + by);
}

/// At the damage routine's entry: zero the damage word when the target is
/// a party member. The program's own routine then runs on zero.
///
/// The frame is checked before it is edited, and the seam declines rather
/// than writing when the check fails (`seam_context::decline`, #96's
/// fail-closed rule). What is checked is the one thing that cannot be
/// argued with: a character record is a far pointer into the program's
/// own memory, and **nothing lives in segment 0** — that is the interrupt
/// vector table and the BDA (`machine/memory_map.h`). A frame whose
/// record argument reads `0000:0004` is not the frame these facts
/// describe, and a zero written into the word eight bytes up from SP is
/// then a zero written into whatever happens to be there.
///
/// That is not hypothetical. Driven against the real program (#103,
/// `docs/playable.md`), this point fires four times in a whole encounter
/// with exactly such frames, and the party's hit points come out
/// *different* from a run with the seam off — neither invulnerable nor
/// untouched, which is the worst of the three. The check makes the seam
/// say so once instead: one `inert point_not_recognized` line, and the
/// program left alone. Finding the routine this offset was meant to name
/// is #99's to finish.
void spare_the_party(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];

  const std::uint16_t record_offset =
      cpu.read_word(ss, word_after(sp, frame_record_offset));
  const std::uint16_t record_segment =
      cpu.read_word(ss, word_after(sp, frame_record_segment));
  if (record_segment == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const auto side = static_cast<std::int8_t>(
      cpu.read_byte(record_segment, word_after(record_offset, rec_side)));
  if (side != side_party) {
    // An enemy's damage is the program's business.
    return;
  }
  cpu.write_word(ss, word_after(sp, frame_damage), 0);
}

/// Deal `debug_damage` to every standing non-party combatant on the
/// roster, exactly as the program's own damage routine deals damage to
/// one — and down the ones it finishes, exactly as the routine downs
/// one.
///
/// Both of this seam's points do this and nothing else; what differs
/// between them is what each is willing to believe before calling it.
void strike_the_enemies(cpu::processor& cpu, std::uint16_t ds) {
  std::uint16_t offset = cpu.read_word(ds, data_roster_head);
  std::uint16_t segment = cpu.read_word(ds, word_after(data_roster_head, 2));
  for (unsigned walked = 0;
       walked < max_roster_walk && (offset != 0 || segment != 0); ++walked) {
    if (segment == 0) {
      // A link that is not a pointer: nothing lives in segment 0, which
      // is the interrupt vector table and the BDA
      // (`machine/memory_map.h`). The walk stops rather than writing a
      // wound status into somebody's interrupt vector — the same
      // argument `spare_the_party` makes about a record on the stack,
      // which this walk had never made about a record in the list.
      return;
    }
    const auto side = static_cast<std::int8_t>(
        cpu.read_byte(segment, word_after(offset, rec_side)));
    const bool standing =
        still_standing(cpu.read_byte(segment, word_after(offset, rec_status)));

    if (side != side_party && standing) {
      const std::uint8_t hit_points =
          cpu.read_byte(segment, word_after(offset, rec_hit_points));
      if (hit_points > debug_damage) {
        // Still in the fight, and therefore still in its side's count:
        // the remainder goes back and **nothing else changes**. That is
        // what the program's own routine does with a character who
        // survives, and it is the branch that makes a body count kept by
        // hand stay right — the program decrements when a body drops, so
        // a seam that decremented for a survivor would put the count
        // below the number of bodies.
        cpu.write_byte(segment, word_after(offset, rec_hit_points),
                       static_cast<std::uint8_t>(hit_points - debug_damage));
      } else {
        // Down, the way the routine downs one. Cleared **held** is what
        // ends the combat: the end check re-tallies both sides' counts
        // from the held bytes, by walking this same roster, immediately
        // before it reads them, so a body left held would come back into
        // the count however many times it had been decremented for.
        //
        // The decrement was faithfulness rather than mechanism while
        // this seam only ever fired *at* that re-tally (#99). It is not
        // any more (#163). Firing mid-round leaves the program running
        // on a count it keeps by hand until the next rebuild, and
        // anything that reads the count before then — a turn's targeting,
        // a "is anybody left" test — reads what this leaves. So the
        // decrement is load-bearing now, and the survivor branch above
        // not decrementing is half of the same rule.
        cpu.write_byte(segment, word_after(offset, rec_status), status_slain);
        cpu.write_byte(segment, word_after(offset, rec_held), 0);
        cpu.write_byte(segment, word_after(offset, rec_hit_points), 0);

        // The side's body count, indexed by the signed side byte exactly
        // as the program indexes it.
        const auto count_at = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(data_side_counts) + side);
        cpu.write_byte(
            ds, count_at,
            static_cast<std::uint8_t>(cpu.read_byte(ds, count_at) - 1));

        const std::uint16_t scratch_offset =
            cpu.read_word(segment, word_after(offset, rec_scratch_offset));
        const std::uint16_t scratch_segment =
            cpu.read_word(segment, word_after(offset, rec_scratch_offset + 2));
        if (scratch_offset != 0 || scratch_segment != 0) {
          cpu.write_byte(scratch_segment,
                         word_after(scratch_offset, scratch_downed), 0);
        }
      }
    }

    const std::uint16_t next_offset =
        cpu.read_word(segment, word_after(offset, rec_next_offset));
    const std::uint16_t next_segment =
        cpu.read_word(segment, word_after(offset, rec_next_offset + 2));
    offset = next_offset;
    segment = next_segment;
  }
}

/// Whether the machine, at this arbitrary step boundary, is one this seam
/// is willing to write to — the guard the address-free point has instead
/// of an address (#163).
///
/// Read-only, and every one of these is a fact this file already knew:
///
///   1. the game mode byte reads `mode_combat`;
///   2. the roster head is a far pointer, and **nothing lives in
///      segment 0** — that is the interrupt vector table and the BDA
///      (`machine/memory_map.h`), the same argument `spare_the_party`
///      makes about a record on the stack;
///   3. every link in the list is a far pointer too, by the same
///      argument, and the list **ends** within `max_roster_walk` rather
///      than being cut off by it;
///   4. somebody on the party's side is standing, and somebody who is
///      not is standing — a combat roster with a fight still in it;
///   5. and for each of those, the program's own body count for that
///      side is non-zero, so the two structures this seam writes agree
///      with each other before it writes either.
///
/// One byte is not a guard: 5 is a plausible value for an uninitialized
/// byte, and a data segment that has not been set up yet holds plenty of
/// them. Five conditions across three structures is a different claim,
/// and the ones that carry it are 3 and 5 — garbage is very unlikely to
/// be a null-terminated list of far pointers whose side bytes index
/// non-zero counters.
///
/// **What it cannot rule out**, and this is the honest limit: that the
/// program is itself part-way through walking this roster — its own
/// re-tally at the top of a round, or a targeting pass — with a record
/// pointer or a running count in a register. This guard reads the same
/// structures that code reads and cannot see its registers. Two things
/// make that survivable rather than fatal. The state left behind is the
/// state the program's own damage routine leaves several times a round,
/// mid-walk or not; and any count this puts wrong is rebuilt from the
/// held bytes at the end check, before it is read to decide the combat.
/// A stale count *within* the round is the real exposure, and it is the
/// reason the decrement in `strike_the_enemies` is not optional.
///
/// **What it assumes**, so the next person can check it: that the party
/// is on this roster. Everything in this file reads as though it is —
/// the walk exists to tell party from enemy — and the fact table says
/// side 0 is the party's, but nobody has watched it. If that is wrong,
/// condition 4 never holds, this point declines and says so once, and
/// the end check serves the pull as it did before.
[[nodiscard]] bool combat_roster_ready(cpu::processor& cpu, std::uint16_t ds) {
  if (cpu.read_byte(ds, data_game_mode) != mode_combat) {
    return false;
  }
  std::uint16_t offset = cpu.read_word(ds, data_roster_head);
  std::uint16_t segment = cpu.read_word(ds, word_after(data_roster_head, 2));

  bool party_standing = false;
  bool enemy_standing = false;
  // One more turn than the walk that writes takes, so that a list of
  // exactly `max_roster_walk` records is seen to *end* rather than to
  // run out.
  for (unsigned walked = 0; walked <= max_roster_walk; ++walked) {
    if (offset == 0 && segment == 0) {
      return party_standing && enemy_standing;
    }
    if (segment == 0) {
      return false;  // a link that is not a pointer
    }
    const auto side = static_cast<std::int8_t>(
        cpu.read_byte(segment, word_after(offset, rec_side)));
    if (still_standing(
            cpu.read_byte(segment, word_after(offset, rec_status)))) {
      const auto count_at = static_cast<std::uint16_t>(
          static_cast<std::int32_t>(data_side_counts) + side);
      if (cpu.read_byte(ds, count_at) == 0) {
        // A body is up and its side's count says nobody is. The two
        // structures disagree, which is what being part-way through
        // something looks like from out here.
        return false;
      }
      if (side == side_party) {
        party_standing = true;
      } else {
        enemy_standing = true;
      }
    }
    const std::uint16_t next_offset =
        cpu.read_word(segment, word_after(offset, rec_next_offset));
    const std::uint16_t next_segment =
        cpu.read_word(segment, word_after(offset, rec_next_offset + 2));
    offset = next_offset;
    segment = next_segment;
  }
  return false;  // it did not end
}

/// The address-free point (#163): offered at every step boundary while
/// the pull is outstanding, acting at the first one where the guard
/// holds and declining — which keeps the latch — at every one before it.
void fell_the_enemies_now(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  if (!combat_roster_ready(cpu, ds)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  strike_the_enemies(cpu, ds);
}

/// At the end check's entry, in combat: the same damage, and let the
/// check find the enemies' side count at zero.
///
/// The guard here is the old one — the mode byte, and a roster head that
/// is a pointer — because the address is most of the evidence and has
/// been driven against the real program. Outside combat it **declines**
/// rather than returning: doing nothing and calling the pull served
/// would answer a person's request with nothing at all, and since #163
/// the pull has somewhere better to be served anyway.
void fell_the_enemies(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  if (cpu.read_byte(ds, data_game_mode) != mode_combat ||
      cpu.read_word(ds, word_after(data_roster_head, 2)) == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  strike_the_enemies(cpu, ds);
}

constexpr std::array<seam_point, 1> invulnerable_points{
    {{.module = resident_image,
      .offset = damage_routine_offset,
      .run = &spare_the_party}}};

/// Two points, both in the module the end check lives in, and the module
/// is what they have in common rather than an accident: the address-free
/// one is address-free, not qualifier-free, and the engine will not offer
/// it while the program's own record says that module is not loaded. So
/// "the tactical combat code is in memory" is a precondition of the
/// immediate point too, and this seam's `armed` row goes on meaning
/// exactly what it meant.
///
/// The immediate one first, so that a step which is both — the end
/// check, with a pull outstanding — is served by the point that answers
/// what the person asked for.
constexpr std::array<seam_point, 2> kill_all_points{
    {{.module = overlay_module,
      .run = &fell_the_enemies_now,
      .at_every_step = true},
     {.module = overlay_module,
      .offset = end_check_offset,
      .run = &fell_the_enemies}}};

constexpr seam_definition invulnerable_definition{
    .id = "cheat-invulnerable",
    .about = "the party takes no damage (debug cheat)",
    .fingerprints = cheat_binaries,
    .points = invulnerable_points,
    .schema = seam_schema_version};

constexpr seam_definition kill_all_definition{
    .id = "cheat-kill-all",
    .about =
        "when you pull it, every enemy takes 120 damage at once (debug "
        "cheat)",
    .fingerprints = cheat_binaries,
    .points = kill_all_points,
    // Pulled, not left on (#161), and served where the person is rather
    // than at the end of the round (#163). The `about` says *when* as
    // well as that it is asked for, because both halves of the original
    // complaint were about when: "it should not decide every fight" and
    // "it should be immediate". "At once" is the claim the address-free
    // point makes and the end check is the fallback underneath it, which
    // is a sentence for the docs rather than for a listing row.
    .trigger = true,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& cheat_invulnerable_seam() noexcept {
  return invulnerable_definition;
}

const seam_definition& cheat_kill_all_seam() noexcept {
  return kill_all_definition;
}

}  // namespace amberfolio::machine
