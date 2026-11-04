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
// **A death's pause is its sound's length.** In a fight, the routine that
// takes a downed figure off the field (overlay 31, load-segment word
// `0x3E10`) turns the music gate off, which leaves the sound driver
// running with nobody waiting on it, starts the death's sound, and calls
// the pause, returning to `0x1018` (or `0x0E34` outside a fight); after
// it, the gate goes back on, and that stops the driver and the sound with
// it. So that pause is how much of the sound is heard: a quarter of a
// second at game speed 1. Lengthened, the player hears the rest of an
// effect the game always cut short (heard on the first build). Those
// two calls are left at the game's length, and a key does not end them.
//
//
// What the seam does
// ------------------
//
// At the `push ax`, it raises AX to the setting when the setting is
// longer, so `Delay` spins for that many milliseconds, unless the caller
// is the script's opcode in the middle of an approach or a death's sound.
// A longer game speed than the setting is left as the player chose it.
//
// At the `dec dx`, when the far return on the stack is the pause's own,
// its caller is not one of those, and a key is waiting in the BIOS buffer
// at 40:1Eh, it takes the key off the buffer and sets DX to one, so the
// loop ends at this millisecond. `Delay` leaves BP alone, so the pause's
// frame, and its caller's far return, are still at `[BP+2]` there.
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
// One wipe without a pause: the encounter menu
// ---------------------------------------------
//
// **What the program does.** The script opcode that offers COMBAT, WAIT,
// FLEE and PARLAY or ADVANCE (overlay 3, its routine at `0x1EF8`) prints
// its own line in the text box before the menu, through the box printer
// at `0x2176` (a far call, the clear flag the fourth word pushed, at
// `[SP+4]`), and clears the box first when that line is not empty. A
// script that has just printed "YOU SPOT A GROUP OF ..." and goes
// straight into the menu has its line wiped as soon as it is typed, with
// no pause between: a third of a second on the screen, and no pause for
// this seam to lengthen (a player's report on the first build). The
// script's own lines go through the print opcode's two calls to the same
// printer, overlay 3 `0x0A69` (append) and `0x0A91` (clear), with the
// line, a Pascal string, at DGROUP `0x70C8`. Every key the program takes
// goes through the runtime's `ReadKey`, image offset `0xA70F`.
//
// **What the seam does.** It keeps one word: whether a line with anything
// but spaces in it has been printed since the program last read a key or
// paused. The print opcode's two calls set it; `ReadKey` and the pause's
// `push ax` clear it. At the menu's own call, when it is set and the call
// clears the box, it waits the setting before letting the call go: it
// calls the runtime's `Delay` (paragraph `0xA40` of the image, offset
// `0x029C`, one word, `retf 2`) fifty milliseconds at a time, and at each
// return, back at the same instruction, takes a waiting key and stops
// waiting. Fifty at a time, rather than all at once, because a call into
// the program is a batch and a batch offers no points: the countdown
// point that ends a pause on a key is not reached inside one.
//
//
// On Curse of the Azure Bonds (#555)
// -----------------------------------
//
// The same routines at Curse's own addresses (`curse_message_pause`),
// with two differences. **Its `Delay` runs short**: the runtime counts
// the spins in one BIOS tick in sixteen bits, which saturates at the 386
// governor, so a millisecond there is 0.6 of one and the game's pause at
// game speed 4 (400) is 241 ms. Raising the count alone would hold a
// line for 0.6 of the setting, so the `push ax` also keeps the moment
// the setting ends at, and the countdown, at the count's last
// millisecond before that moment, gives it one more; the wait before
// the encounter menu is held to a moment the same way. **And its script
// has an animation step** (the next frame of a picture, then the pause),
// which keeps its length as an approach's beat does. The death
// routine's two pauses keep theirs as on Pool of Radiance: no gate ends
// the sound there, but it runs on until the next sound, and lengthened
// it would be heard far longer than the program ever plays it.
//
//
// What it is not yet (docs/seams.md §8.5)
// ---------------------------------------
//
// A scripted line that another line replaces, or a picture or a change of
// map wipes, with no pause between is not given one: only the encounter
// menu's wipe is.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// This seam's facts about one program (#555): the shared ones through
/// its profile (program_profile.h), and the ones only this seam reads.
/// One row per program it is available on; every handler is
/// instantiated on one row, so a handler never asks which program it is
/// on (the cheats' pattern, seam_cheats.cpp).
struct message_pause_facts {
  const program_profile& profile;

  /// The pause's `push ax`, and the far return of its call to `Delay`,
  /// from the image segment.
  std::uint32_t push_offset;
  std::uint32_t pause_return_offset;

  /// `Delay`'s `dec dx`, from the image segment.
  std::uint32_t countdown_offset;

  /// `Delay` itself: its paragraph, counted from the image segment, and
  /// its offset there. One word, `retf 2`.
  std::uint16_t delay_paragraphs;
  std::uint16_t delay_offset;

  /// The script opcode's call, in the interpreter overlay (the
  /// profile's): the offset the call returns to.
  std::uint16_t script_return_offset;

  /// In the same overlay: the print opcode's two calls to the box
  /// printer, and the encounter menu's.
  std::uint32_t print_append_call;
  std::uint32_t print_clear_call;
  std::uint32_t encounter_print_call;

  /// The line the print opcode prints, a Pascal string in the data
  /// segment.
  std::uint16_t data_print_line;

  /// The far pointer to the game-state block, and the approach's count in
  /// it.
  std::uint16_t data_state_block;
  std::uint16_t state_approach_frames;

  /// The death routine's calls, in the module whose load-segment word is
  /// the profile's `death_load_segment_at`: the offsets the calls return
  /// to.
  std::array<std::uint16_t, 2> death_return_offsets;

  /// In the interpreter overlay: the offset the script's animation step
  /// returns to from its pause (the next frame of the picture, then a
  /// pause), or zero on a program whose rows name none.
  std::uint16_t animation_return_offset;

  /// Whether the runtime's millisecond can be shorter than one. `Delay`
  /// spins a count the runtime measured at start-up over one BIOS tick,
  /// in sixteen bits; a governor fast enough to spin more than 65,535
  /// times in a tick saturates it, and every millisecond after is that
  /// much short. On such a program the seam holds a pause by virtual time
  /// and not by the count alone.
  bool delay_runs_short;
};

/// Pool of Radiance.
constexpr message_pause_facts pool_of_radiance_message_pause{
    .profile = pool_of_radiance,
    .push_offset = 0x7E5B,
    .pause_return_offset = 0x7E61,
    .countdown_offset = 0xA6B7,
    .delay_paragraphs = 0xA40,
    .delay_offset = 0x029C,
    .script_return_offset = 0x272B,
    .print_append_call = 0x0A69,
    .print_clear_call = 0x0A91,
    .encounter_print_call = 0x2176,
    .data_print_line = 0x70C8,
    .data_state_block = 0x49D6,
    .state_approach_frames = 0x582,
    .death_return_offsets =
        {
            0x1018,  // in a fight
            0x0E34,  // anywhere else
        },
    .animation_return_offset = 0,
    .delay_runs_short = false};

/// Curse of the Azure Bonds (#555). The same routine at its own address
/// (the game speed times 100 rather than 225), the same runtime `Delay`
/// and `ReadKey`, and the same interpreter opcodes. Two things differ:
/// the runtime's millisecond is 0.6 of one at the 386 governor, and the
/// script has an animation step of its own.
constexpr message_pause_facts curse_message_pause{
    .profile = curse_of_the_azure_bonds,
    .push_offset = 0x5806,
    .pause_return_offset = 0x580C,
    .countdown_offset = 0x86A9,
    .delay_paragraphs = 0x83F,
    .delay_offset = 0x029C,
    .script_return_offset = 0x2917,
    .print_append_call = 0x0A7F,
    .print_clear_call = 0x0AAA,
    .encounter_print_call = 0x235F,
    .data_print_line = 0x7762,
    .data_state_block = 0x4FB6,
    .state_approach_frames = 0x582,
    .death_return_offsets =
        {
            0x10D4,  // in a fight
            0x0E8A,  // anywhere else
        },
    .animation_return_offset = 0x3090,
    .delay_runs_short = true};

constexpr auto message_pause_binaries =
    fingerprints_of(std::array{&pool_of_radiance, &curse_of_the_azure_bonds});

/// The encounter menu's call: the clear flag is the fourth word pushed.
constexpr std::uint16_t call_clear_flag = 4;

/// How long one call of the wait before the menu is, in milliseconds.
constexpr std::uint16_t wait_slice = 50;

/// The seam's own words.
constexpr unsigned word_unread = 0;     // a line printed, not yet waited on
constexpr unsigned word_remaining = 1;  // of the wait before the menu, ms
/// On a program whose `Delay` runs short: the moment the pause may end,
/// and the moment the wait before the menu does, four words each, low
/// word first. Zero is "no moment".
constexpr unsigned word_pause_end = 2;
constexpr unsigned word_menu_end = 6;

/// The pause's frame: BP at SP, the far return above it.
constexpr std::uint16_t frame_return = 2;

/// What `seam_setting` says this seam takes.
constexpr std::uint16_t pause_minimum = 0;
constexpr std::uint16_t pause_maximum = 10000;
constexpr std::uint16_t pause_step = 250;
constexpr std::uint16_t pause_initial = 1500;

template <const message_pause_facts& F>
[[nodiscard]] std::uint16_t dgroup_of(const seam_context& ctx) {
  return static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                    F.profile.dgroup_paragraphs);
}

[[nodiscard]] std::uint32_t linear(std::uint16_t segment,
                                   std::uint16_t offset) {
  return (static_cast<std::uint32_t>(segment) << 4U) + offset;
}

/// A moment kept in four of the seam's words.
[[nodiscard]] ticks moment_in(const seam_context& ctx, unsigned first) {
  ticks moment = 0;
  for (unsigned word = 4; word-- > 0;) {
    moment = (moment << 16U) | ctx.scratch(first + word);
  }
  return moment;
}

void keep_moment(seam_context& ctx, unsigned first, ticks moment) {
  for (unsigned word = 0; word < 4; ++word) {
    ctx.set_scratch(first + word,
                    static_cast<std::uint16_t>(moment >> (16U * word)));
  }
}

/// `milliseconds` of virtual time from now.
[[nodiscard]] ticks after(const machine& box, std::uint16_t milliseconds) {
  return box.time() + (ticks{milliseconds} * pit_input_hz) / 1000U;
}

/// The interpreter overlay's segment now, or zero while it is not loaded.
template <const message_pause_facts& F>
[[nodiscard]] std::uint16_t script_segment(cpu::processor& cpu,
                                           const seam_context& ctx) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  return cpu.read_word(image,
                       static_cast<std::uint16_t>(
                           F.profile.interpreter_overlay.load_segment_at));
}

/// Whether the pause was called by the script opcode while an approach
/// is still counting its frames.
template <const message_pause_facts& F>
[[nodiscard]] bool approach_beat(cpu::processor& cpu, const seam_context& ctx,
                                 std::uint16_t ds, std::uint16_t segment,
                                 std::uint16_t offset) {
  const std::uint16_t script = script_segment<F>(cpu, ctx);
  if (script == 0 ||
      linear(segment, offset) != linear(script, F.script_return_offset)) {
    return false;
  }
  const std::uint16_t block_offset = cpu.read_word(ds, F.data_state_block);
  const std::uint16_t block_segment =
      cpu.read_word(ds, static_cast<std::uint16_t>(F.data_state_block + 2));
  if (linear(block_segment, block_offset) >= 0xA0000U) {
    // Not a block in conventional memory: no answer, and no read of the
    // video window to find one (docs/seams.md §8.4).
    return false;
  }
  return cpu.read_word(block_segment,
                       static_cast<std::uint16_t>(
                           block_offset + F.state_approach_frames)) != 0;
}

/// Whether the pause was called by the script's animation step, between
/// two frames of a picture.
template <const message_pause_facts& F>
[[nodiscard]] bool animation_beat(cpu::processor& cpu, const seam_context& ctx,
                                  std::uint16_t segment, std::uint16_t offset) {
  if constexpr (F.animation_return_offset == 0) {
    return false;
  } else {
    const std::uint16_t script = script_segment<F>(cpu, ctx);
    return script != 0 &&
           linear(segment, offset) == linear(script, F.animation_return_offset);
  }
}

/// Whether the pause was called by the death routine, whose pause is how
/// long the death's sound is heard.
template <const message_pause_facts& F>
[[nodiscard]] bool death_sound(cpu::processor& cpu, const seam_context& ctx,
                               std::uint16_t segment, std::uint16_t offset) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  const std::uint16_t death =
      cpu.read_word(image, F.profile.death_load_segment_at);
  if (death == 0) {
    return false;
  }
  for (const std::uint16_t site : F.death_return_offsets) {
    if (linear(segment, offset) == linear(death, site)) {
      return true;
    }
  }
  return false;
}

/// Whether the pause called from `segment:offset` is not a message's, and
/// keeps the game's length.
template <const message_pause_facts& F>
[[nodiscard]] bool keeps_its_length(cpu::processor& cpu,
                                    const seam_context& ctx, std::uint16_t ds,
                                    std::uint16_t segment,
                                    std::uint16_t offset) {
  return death_sound<F>(cpu, ctx, segment, offset) ||
         approach_beat<F>(cpu, ctx, ds, segment, offset) ||
         animation_beat<F>(cpu, ctx, segment, offset);
}

template <const message_pause_facts& F>
void lengthen_the_pause(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  if (ds != dgroup_of<F>(ctx) || bp != regs[cpu::reg16::sp]) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  // A pause, lengthened or not, is time the line already had.
  ctx.set_scratch(word_unread, 0);
  const std::uint16_t wanted = ctx.setting();
  if constexpr (F.delay_runs_short) {
    // The count says nothing about how long the pause is, so the game
    // speed's count is no answer either: the moment is. Raise the count
    // as Pool of Radiance's row does, so that a millisecond that is a
    // millisecond needs nothing more, and keep the moment the countdown
    // holds it to.
    keep_moment(ctx, word_pause_end, 0);
    if (wanted == 0) {
      return;
    }
  } else if (regs[cpu::reg16::ax] >= wanted) {
    // The game speed is already at least this long.
    return;
  }
  const std::uint16_t return_offset =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return));
  const std::uint16_t return_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return + 2));
  if (keeps_its_length<F>(cpu, ctx, ds, return_segment, return_offset)) {
    return;
  }
  if constexpr (F.delay_runs_short) {
    keep_moment(ctx, word_pause_end, after(box, wanted));
  }
  if (regs[cpu::reg16::ax] < wanted) {
    regs[cpu::reg16::ax] = wanted;
  }
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

template <const message_pause_facts& F>
void end_on_a_key(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const std::uint16_t return_offset = cpu.read_word(ss, sp);
  const std::uint16_t return_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(sp + 2));
  if (linear(return_segment, return_offset) !=
      ctx.image_base() + F.pause_return_offset) {
    // Some other wait: the typing, the music, a beat a routine times
    // itself.
    return;
  }
  const std::uint16_t ds = regs[cpu::sreg::ds];
  if (ds != dgroup_of<F>(ctx)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t caller_offset =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return));
  const std::uint16_t caller_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return + 2));
  if (keeps_its_length<F>(cpu, ctx, ds, caller_segment, caller_offset)) {
    return;
  }
  if (cpu.read_word(bda::segment, bda::keyboard_buffer_head) ==
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail)) {
    if constexpr (F.delay_runs_short) {
      // The count's last millisecond, and the moment not reached: one
      // more. DX is the count before this `dec dx`.
      if (regs[cpu::reg16::dx] == 1 &&
          box.time() < moment_in(ctx, word_pause_end)) {
        regs[cpu::reg16::dx] = 2;
      }
    }
    return;
  }
  take_key(cpu);
  regs[cpu::reg16::dx] = 1;
  if constexpr (F.delay_runs_short) {
    keep_moment(ctx, word_pause_end, 0);
  }
}

/// Whether a key is waiting in the BIOS buffer.
[[nodiscard]] bool key_waiting(cpu::processor& cpu) {
  return cpu.read_word(bda::segment, bda::keyboard_buffer_head) !=
         cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
}

template <const message_pause_facts& F>
void note_a_printed_line(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  if (ds != dgroup_of<F>(ctx)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint8_t length = cpu.read_byte(ds, F.data_print_line);
  for (std::uint16_t at = 1; at <= length; ++at) {
    if (cpu.read_byte(ds, static_cast<std::uint16_t>(F.data_print_line + at)) !=
        ' ') {
      ctx.set_scratch(word_unread, 1);
      return;
    }
  }
}

void note_a_key_read(machine& /*box*/, seam_context& ctx) {
  ctx.set_scratch(word_unread, 0);
}

template <const message_pause_facts& F>
void wait_before_the_menu(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  if (regs[cpu::sreg::ds] != dgroup_of<F>(ctx)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  std::uint16_t remaining = ctx.scratch(word_remaining);
  if (remaining == 0) {
    const std::uint8_t clears = cpu.read_byte(
        regs[cpu::sreg::ss],
        static_cast<std::uint16_t>(regs[cpu::reg16::sp] + call_clear_flag));
    if (ctx.scratch(word_unread) == 0 || clears == 0) {
      // Nothing unread, or the menu prints beneath it and wipes nothing.
      return;
    }
    ctx.set_scratch(word_unread, 0);
    remaining = ctx.setting();
    if (remaining == 0) {
      return;
    }
    if constexpr (F.delay_runs_short) {
      // The wait is held to a moment; the word says only that it is on.
      keep_moment(ctx, word_menu_end, after(box, remaining));
      remaining = wait_slice;
    }
  } else if (key_waiting(cpu)) {
    take_key(cpu);
    ctx.set_scratch(word_remaining, 0);
    return;
  } else if constexpr (F.delay_runs_short) {
    if (box.time() >= moment_in(ctx, word_menu_end)) {
      ctx.set_scratch(word_remaining, 0);
      return;
    }
    remaining = wait_slice;
  }
  const std::uint16_t slice = remaining < wait_slice ? remaining : wait_slice;
  const std::array<std::uint16_t, 1> milliseconds{slice};
  const auto delay =
      static_cast<std::uint16_t>((ctx.image_base() >> 4U) + F.delay_paragraphs);
  if (!ctx.call_program(delay, F.delay_offset, milliseconds)) {
    ctx.set_scratch(word_remaining, 0);
    return;
  }
  if constexpr (F.delay_runs_short) {
    // Not a count: the moment ends it.
    ctx.set_scratch(word_remaining, 1);
  } else {
    ctx.set_scratch(word_remaining,
                    static_cast<std::uint16_t>(remaining - slice));
  }
}

/// The points of one program's row: the pause and `Delay`'s countdown
/// and `ReadKey` in the resident image, the print opcode's and the
/// encounter menu's calls in the interpreter overlay.
template <const message_pause_facts& F>
constexpr std::array<seam_point, 6> message_pause_points{{
    {.module = resident_image,
     .offset = F.push_offset,
     .run = &lengthen_the_pause<F>},
    {.module = resident_image,
     .offset = F.countdown_offset,
     .run = &end_on_a_key<F>},
    {.module = resident_image,
     .offset = F.profile.key_read_entry,
     .run = &note_a_key_read},
    {.module = F.profile.interpreter_overlay,
     .offset = F.print_append_call,
     .run = &note_a_printed_line<F>},
    {.module = F.profile.interpreter_overlay,
     .offset = F.print_clear_call,
     .run = &note_a_printed_line<F>},
    {.module = F.profile.interpreter_overlay,
     .offset = F.encounter_print_call,
     .run = &wait_before_the_menu<F>},
}};

/// The points on every program but the baseline, whose points are the
/// definition's own (seam.h, schema 9).
constexpr std::array<seam_program_points, 1> message_pause_programs{
    {{.fingerprint = curse_of_the_azure_bonds.fingerprint,
      .points = message_pause_points<curse_message_pause>}}};

constexpr seam_definition message_pause_definition{
    .id = "message-pause",
    .about = "a message stays up long enough to read; any key moves it on",
    .fingerprints = message_pause_binaries,
    .points = message_pause_points<pool_of_radiance_message_pause>,
    .programs = message_pause_programs,
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
