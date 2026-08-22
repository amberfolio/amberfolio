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
// in the resident image with two arguments pushed left to right: the
// damage as a word, then the record as a far pointer. At its entry the
// stack holds the return address, then the record's offset and segment,
// then the damage word. The routine subtracts, sets the wound status by
// how far below zero the record went, writes the remaining hit points
// back if the character is still standing, and otherwise downs them:
// clears the flag, zeroes the hit points, decrements the side's body
// count, and clears a byte in the scratch block.
//
// **Combat ends in one overlaid routine.** Once a round, the tactical
// loop calls an end check that lives in overlay 8 of the overlay file;
// it walks the roster, ages the dying, and answers "over" when either
// side's body count has reached zero. The overlay manager loads that
// overlay with one read of the code block — `overlay_module` below is
// the file, the offset and the length of that read, and the SHA-256 of
// the bytes it delivers, as the tracker records it (overlay.h).
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
// **Kill-all-enemies** intercepts the end check's entry — the point at
// which the program will next consult the combat state, which is what
// #99 asks for — and, when the game mode says combat, downs every enemy
// exactly as the damage routine downs one: slain, flag cleared, hit
// points zero, the side's body count decremented, the scratch byte
// cleared. The program's own end check then finds the enemies' count at
// zero and ends the combat through its own logic, and its own post-combat
// code runs unmodified. Outside combat the point does nothing, and a
// roster walk is bounded so a record whose next pointer is not what the
// facts say cannot spin the host.
//
// Both are fail-closed by construction (#99): unavailable on any binary
// but the baseline's (the fingerprint), inert with a reported reason
// while overlay 8 is not resident (kill-all's point, #97), and nothing on
// the hot path when off (#96).

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
constexpr std::uint16_t rec_standing = 0x10D;  // non-zero while up and counted
constexpr std::uint16_t rec_side = 0x10E;      // combat side, signed
constexpr std::uint16_t rec_hit_points = 0x11B;

/// The wound status that means slain.
constexpr std::uint8_t status_slain = 6;

/// The side index that is the party's. Signed, because the record's byte
/// is the signed index the program uses into the body counts.
constexpr std::int8_t side_party = 0;

/// The byte of the combat scratch block the program clears when a
/// combatant is downed.
constexpr std::uint16_t scratch_downed = 3;

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

/// The stack at that entry, as offsets from SP: the return address, then
/// the record's far pointer (offset, then segment), then the damage word
/// — the arguments in the order they were pushed, left to right.
constexpr std::uint16_t frame_record_offset = 4;
constexpr std::uint16_t frame_record_segment = 6;
constexpr std::uint16_t frame_damage = 8;

// --- Where combat ends -----------------------------------------------------

/// Overlay 8 of the overlay file, as the manager reads it: one read of
/// the code block, whose facts the tracker records (overlay.h). The
/// digest is of those bytes as read, so a copy whose overlay file does
/// not match leaves the seam inert rather than pointed at the wrong code.
constexpr seam_module overlay_module{
    .file = "GAME.OVR",
    .file_offset = 38919,
    .length = 4735,
    .digest =
        "5d07a6b3fedb56509214f24bdbdbc3b8625ddf6b2ce4d6274e6e89b26c563930"};

/// The end check's entry, as an offset from where that read landed.
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

/// At the end check's entry, in combat: down every enemy the way the
/// damage routine downs one, and let the check find their side's count at
/// zero.
void fell_the_enemies(machine& box, seam_context& /*ctx*/) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  if (cpu.read_byte(ds, data_game_mode) != mode_combat) {
    return;
  }

  std::uint16_t offset = cpu.read_word(ds, data_roster_head);
  std::uint16_t segment = cpu.read_word(ds, word_after(data_roster_head, 2));
  for (unsigned walked = 0;
       walked < max_roster_walk && (offset != 0 || segment != 0); ++walked) {
    const auto side = static_cast<std::int8_t>(
        cpu.read_byte(segment, word_after(offset, rec_side)));
    const bool standing =
        cpu.read_byte(segment, word_after(offset, rec_standing)) != 0;

    if (side != side_party && standing) {
      cpu.write_byte(segment, word_after(offset, rec_status), status_slain);
      cpu.write_byte(segment, word_after(offset, rec_standing), 0);
      cpu.write_byte(segment, word_after(offset, rec_hit_points), 0);

      // The side's body count, indexed by the signed side byte exactly as
      // the program indexes it.
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

    const std::uint16_t next_offset =
        cpu.read_word(segment, word_after(offset, rec_next_offset));
    const std::uint16_t next_segment =
        cpu.read_word(segment, word_after(offset, rec_next_offset + 2));
    offset = next_offset;
    segment = next_segment;
  }
}

constexpr std::array<seam_point, 1> invulnerable_points{
    {{.module = resident_image,
      .offset = damage_routine_offset,
      .run = &spare_the_party}}};

constexpr std::array<seam_point, 1> kill_all_points{
    {{.module = overlay_module,
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
    .about = "every enemy falls at the end of the round (debug cheat)",
    .fingerprints = cheat_binaries,
    .points = kill_all_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& cheat_invulnerable_seam() noexcept {
  return invulnerable_definition;
}

const seam_definition& cheat_kill_all_seam() noexcept {
  return kill_all_definition;
}

}  // namespace amberfolio::machine
