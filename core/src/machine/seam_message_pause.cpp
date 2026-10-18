// SPDX-License-Identifier: AGPL-3.0-only
//
// The message-pause seam: a message the program shows and moves on from
// without a key stays up long enough to read, and any key moves it on.
//
// **What a player meets.** With it off, a combat line ("... ATTACKS ...
// HITTING FOR 6 POINTS OF DAMAGE"), a flashed warning and a scripted
// line followed by the script's own pause each stay up for the game's
// pause, a quarter of a second at the game speed every shipped save
// holds, plus however long the program then takes to draw the next
// thing. On the machine the game was written for the drawing was most of
// it; at the 386 governor it is next to nothing, and a combat line that
// stood for nearly three seconds on an XT stands for half of one. With it
// on, each of those pauses lasts at least the seam's setting, 1500 ms
// unless the player chose otherwise, and a key ends it early: the key is
// taken, so it does not go on to answer the next prompt as well.
//
// The game's own speed setting (Encamp, Alter, Speed) slows everything
// it scales, the walk and the animations as much as the reading. This
// moves only the pause after a message.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of this is addresses and offsets (CONTRIBUTING.md). Nothing of the
// program is reproduced here.
//
// **One routine is the message pause.** At image offset `0x7E4E` (its
// paragraph is the image's `0x709` on), `push bp; mov bp,sp`, then the
// game-speed byte at DGROUP `0x49E2` times 225 into AX, `push ax` at
// `0x7E5B`, and a far call into the runtime's `Delay(ms)`, whose far
// return is image offset `0x7E61`; `pop bp; retf`. Every combat line, the
// green and yellow flashed warnings, the script opcode that pauses
// between two lines, and the cinematic beats at a pool's cleansing and
// the ending come through it. Typing a line out, a character at a time,
// does not: that calls `Delay` directly, with a tenth of the speed, and
// is left as it is.
//
// **`Delay` is a calibrated spin.** At image offset `0xA69C`: it takes the
// count of milliseconds from its frame and, for each, spins a loop count
// the runtime measured against the BIOS tick when the program started,
// then `dec dx` at `0xA6B7` and back while any are left. Nothing is pushed
// between its entry and that `dec dx`, so the far return of whoever
// called it is at `SS:SP` there. The count is a millisecond of virtual
// time at every governor, which is why the program's own pause is exactly
// as long here as on the hardware: the difference a player sees is the
// drawing around it, not the pause.
//
// **A script's pause has a caller of its own.** The script opcode that
// pauses is overlay 3's (load-segment word `0x200`) routine at `0x271F`,
// whose call to the pause returns to `0x272B`. A script pauses between
// the frames of a figure walking up to the party, too: while that
// approach is running its remaining frames are counted in the word at
// `+0x582` of the game-state block DGROUP `0x49D6` points at (a far
// pointer, offset then segment), which the next frame's opcode counts
// down. A pause the opcode makes while the count is not zero is a beat
// of that animation, not a message, and is left at the game's length.
//
//
// What the seam does
// ------------------
//
// At the `push ax`, it raises AX to the setting when the setting is
// longer, so `Delay` spins for that many milliseconds, unless the caller
// is the script's opcode in the middle of an approach. A longer game
// speed than the setting is left as the player chose it.
//
// At the `dec dx`, when the far return on the stack is the pause's own,
// and a key is waiting in the BIOS buffer at 40:1Eh, it takes the key off
// the buffer and sets DX to one, so the loop ends at this millisecond.
// Any `Delay` the pause did not make, the typing and the music included,
// runs as it always did.
//
// Both points check that DS is the program's data segment, and the first
// that BP is the routine's own frame, and decline otherwise.
//
// **What it costs.** A run with it on differs from the run with it off
// from the first message pause, and every clock read after it reads
// another time. Before the first one, it touches nothing. A key a player
// types ahead during a pause is used up ending it.
//
//
// What it is not yet (docs/seams.md §8.5)
// ---------------------------------------
//
// A scripted line the script clears or replaces with no pause at all
// (a few dozen in the game's scripts) is not lengthened: there is no
// pause to lengthen, and making one would be a seam that decided, line by
// line, which text deserves reading time.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about: the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> message_pause_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// The pause's `push ax`, and the far return of its call to `Delay`, from
/// the image segment.
constexpr std::uint32_t push_offset = 0x7E5B;
constexpr std::uint32_t pause_return_offset = 0x7E61;

/// `Delay`'s `dec dx`, from the image segment.
constexpr std::uint32_t countdown_offset = 0xA6B7;

/// The data segment is the image's paragraph `0xC7C` on.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;

/// The script opcode's call: its overlay's load-segment word, from the
/// image segment, and the offset the call returns to.
constexpr std::uint16_t script_load_segment_at = 0x200;  // overlay 3
constexpr std::uint16_t script_return_offset = 0x272B;

/// The far pointer to the game-state block, and the approach's count in
/// it.
constexpr std::uint16_t data_state_block = 0x49D6;
constexpr std::uint16_t state_approach_frames = 0x582;

/// The pause's frame: BP at SP, the far return above it.
constexpr std::uint16_t frame_return = 2;

/// What `seam_setting` says this seam takes.
constexpr std::uint16_t pause_minimum = 0;
constexpr std::uint16_t pause_maximum = 10000;
constexpr std::uint16_t pause_step = 250;
constexpr std::uint16_t pause_initial = 1500;

[[nodiscard]] std::uint16_t dgroup_of(const seam_context& ctx) {
  return static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                    dgroup_paragraphs);
}

[[nodiscard]] std::uint32_t linear(std::uint16_t segment,
                                   std::uint16_t offset) {
  return (static_cast<std::uint32_t>(segment) << 4U) + offset;
}

/// Whether the pause was called by the script opcode while an approach
/// is still counting its frames.
[[nodiscard]] bool approach_beat(cpu::processor& cpu, const seam_context& ctx,
                                 std::uint16_t ds, std::uint16_t segment,
                                 std::uint16_t offset) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  const std::uint16_t script = cpu.read_word(image, script_load_segment_at);
  if (script == 0 ||
      linear(segment, offset) != linear(script, script_return_offset)) {
    return false;
  }
  const std::uint16_t block_offset = cpu.read_word(ds, data_state_block);
  const std::uint16_t block_segment =
      cpu.read_word(ds, static_cast<std::uint16_t>(data_state_block + 2));
  if (linear(block_segment, block_offset) >= 0xA0000U) {
    // Not a block in conventional memory: no answer, and no read of the
    // video window to find one (docs/seams.md §8.4).
    return false;
  }
  return cpu.read_word(block_segment,
                       static_cast<std::uint16_t>(block_offset +
                                                  state_approach_frames)) != 0;
}

void lengthen_the_pause(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  if (ds != dgroup_of(ctx) || bp != regs[cpu::reg16::sp]) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint16_t wanted = ctx.setting();
  if (regs[cpu::reg16::ax] >= wanted) {
    // The game speed is already at least this long.
    return;
  }
  const std::uint16_t return_offset =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return));
  const std::uint16_t return_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return + 2));
  if (approach_beat(cpu, ctx, ds, return_segment, return_offset)) {
    return;
  }
  regs[cpu::reg16::ax] = wanted;
}

/// Take the key at the head of the BIOS buffer. The caller has checked
/// there is one.
void take_key(cpu::processor& cpu) {
  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  auto next = static_cast<std::uint16_t>(head + 2U);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  cpu.write_word(bda::segment, bda::keyboard_buffer_head, next);
}

void end_on_a_key(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const std::uint16_t return_offset = cpu.read_word(ss, sp);
  const std::uint16_t return_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(sp + 2));
  if (linear(return_segment, return_offset) !=
      ctx.image_base() + pause_return_offset) {
    // Some other wait: the typing, the music, a beat a routine times
    // itself.
    return;
  }
  if (regs[cpu::sreg::ds] != dgroup_of(ctx)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (cpu.read_word(bda::segment, bda::keyboard_buffer_head) ==
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail)) {
    return;
  }
  take_key(cpu);
  regs[cpu::reg16::dx] = 1;
}

constexpr std::array<seam_point, 2> message_pause_points{{
    {.module = resident_image,
     .offset = push_offset,
     .run = &lengthen_the_pause},
    {.module = resident_image,
     .offset = countdown_offset,
     .run = &end_on_a_key},
}};

constexpr seam_definition message_pause_definition{
    .id = "message-pause",
    .about = "a message stays up long enough to read; any key moves it on",
    .fingerprints = message_pause_binaries,
    .points = message_pause_points,
    .setting = {.name = "pause",
                .unit = "ms",
                .minimum = pause_minimum,
                .maximum = pause_maximum,
                .step = pause_step,
                .initial = pause_initial},
    .schema = seam_schema_version};

}  // namespace

const seam_definition& message_pause_seam() noexcept {
  return message_pause_definition;
}

}  // namespace amberfolio::machine
