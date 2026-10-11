// SPDX-License-Identifier: AGPL-3.0-only
//
// The quick-steps seam: walking does not wait for the footstep's click
// where the wait is only a wait.
//
// **What a player meets.** With it off, every turn and every step forward
// stops the program for a tenth of a second while the click plays: a turn
// waits *before* the view is redrawn, and a step forward waits twice, once
// before the redraw and once after it. At the 386 governor that is about
// six frames of the eleven between the key and the new view on a turn,
// and five or six more on each side of a step's redraw, the second of
// which keeps the next key waiting. With it on, the click still plays,
// and plays to its end, but a turn does not wait for it (answered in six
// frames) and neither does a step's second click, so the next key is
// taken a tenth of a second sooner.
//
// **A step's first wait stays.** It is what spaces a step's two clicks:
// the first, a tenth of a second of nothing, the redraw, the second.
// Skipped, the two run together, which a player heard as not the game's
// own sound (rejected on the first build).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of this is addresses and offsets (CONTRIBUTING.md). Nothing of the
// program is reproduced here.
//
// **One routine starts every sound.** The music and effects driver's
// dispatch entry, at image offset `0x8BBC`, takes one word, a request
// number, at `[BP+6]`, and the far return at `[BP+2]` (`push bp; mov
// bp,sp` and a `retf 2`). Five calls in the program pass it the word it
// keeps at DGROUP `0x2628`, the footstep, each pushed from that word just
// before the call. As the far returns they leave:
//
//   * overlay 14 (load-segment word `0x730`), `0x0B19` and `0x0B5E`: a turn
//     left and a turn right, before the view is redrawn;
//   * overlay 14, `0x0747`: a step's commit, before the redraw;
//   * overlay 3 (load-segment word `0x200`), `0x34D8`: the adventuring loop,
//     after the redraw, when the party's square changed;
//   * overlay 3, `0x2D91`: an event script's own click.
//
// **It ends with a bracket.** After it has started what was asked for, it
// tests the driver's busy word, DGROUP `0x5374`, and when that is set it
// spins in the driver's own calibrated wait for ten hundredths of a second
// on the DOS clock, then silences both voice banks and stops the driver.
// `bracket_offset` is that test, a `cmp word [0x5374],0` whose operand is
// at `+2`; `epilogue_offset` is the routine's `pop bp; retf 2`, where the
// test's own branch goes when the word is clear. The wait is what costs
// the frames: it is a spin on the clock, not on anything the effect does.
//
// **The effect ends by itself.** The driver's timer service plays the
// click from the voice bank on its own ticks and runs it out to silence
// with nobody waiting; the next request the dispatch entry is handed stops
// whatever is still going first (its own `0x5374` test at the top). So a
// click whose bracket is skipped sounds as long as it always did, unless
// the next one starts first: a step taken the moment the last one's second
// click starts cuts that click short.
//
//
// What the seam does
// ------------------
//
// At the bracket, when the request in the frame is the footstep's and the
// far return is one of the three call sites above that it skips (the two
// turns and the loop's), it moves IP to the epilogue: the dispatch returns
// as it does when nothing is playing. The step's commit, the event
// script's click and every other request (a door, a spell, the music) go
// through the bracket exactly as they always did. A call site's segment is
// its overlay's, read from the word the program's overlay manager keeps it
// in at the moment of the call. It checks that the
// instruction is the test (its operand word is the busy word's address),
// that DS is the program's data segment, and that BP is the routine's own
// frame, and declines otherwise.
//
// **What it costs.** The driver is left running between clicks, which is
// what its own busy word says, until the next request stops it. A run with
// it on differs from the run with it off from the first click: the timer
// service writes the sound chip on more ticks, the program reaches its
// next key sooner, and so everything that reads the clock afterwards reads
// another time. Before the first click, it touches nothing.
//
//
// What it is not yet (docs/seams.md §8.5)
// ---------------------------------------
//
// It removes waits, not the drawing. A step's new view still comes after
// its first click's wait, as the program has it; and of what is left
// between a key and a turned view, some four frames at the 386 governor
// are the program composing the view and copying it out.

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

/// The SHA-256 of the program image these offsets are facts about: the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> quick_steps_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// The dispatch entry's busy-word test, and its `pop bp; retf 2`, both from
/// the image segment.
constexpr std::uint32_t bracket_offset = 0x8C36;
constexpr std::uint32_t epilogue_offset = 0x8C57;

/// The data segment is the image's paragraph `0xC7C` on.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;

/// The driver's busy word, which the test's operand names, and the word
/// the walk's clicks are requested with.
constexpr std::uint16_t data_busy = 0x5374;
constexpr std::uint16_t data_footstep = 0x2628;

/// The far return and the request, in the dispatch entry's frame.
constexpr std::uint16_t frame_return = 2;
constexpr std::uint16_t frame_request = 6;

/// A call site whose wait is skipped: the word its overlay's segment is
/// kept in, from the image segment, and the offset the call returns to.
struct call_site {
  std::uint16_t load_segment_at;
  std::uint16_t return_offset;
};

constexpr std::uint16_t adventure_input_load_segment_at = 0x730;  // overlay 14
constexpr std::uint16_t adventure_loop_load_segment_at = 0x200;   // overlay 3

constexpr std::array<call_site, 3> skipped_sites{{
    {.load_segment_at = adventure_input_load_segment_at,
     .return_offset = 0x0B19},  // turn left
    {.load_segment_at = adventure_input_load_segment_at,
     .return_offset = 0x0B5E},  // turn right
    {.load_segment_at = adventure_loop_load_segment_at,
     .return_offset = 0x34D8},  // after the step's redraw
}};

[[nodiscard]] bool is_a_skipped_site(cpu::processor& cpu,
                                     const seam_context& ctx,
                                     std::uint16_t segment,
                                     std::uint16_t offset) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  for (const call_site& site : skipped_sites) {
    if (offset != site.return_offset) {
      continue;
    }
    const std::uint16_t loaded = cpu.read_word(image, site.load_segment_at);
    if (loaded != 0 && loaded == segment) {
      return true;
    }
  }
  return false;
}

void skip_the_wait(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t cs = regs[cpu::sreg::cs];
  const std::uint16_t ip = regs.ip;
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];

  // DS first: every read below goes through it.
  const auto dgroup =
      static_cast<std::uint16_t>((ctx.image_base() >> 4U) + dgroup_paragraphs);
  if (ds != dgroup ||
      cpu.read_word(cs, static_cast<std::uint16_t>(ip + 2)) != data_busy ||
      bp != regs[cpu::reg16::sp]) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint16_t request =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_request));
  if (request != cpu.read_word(ds, data_footstep)) {
    // Some other sound, which waits as it always did.
    return;
  }
  const std::uint16_t return_offset =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return));
  const std::uint16_t return_segment =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return + 2));
  if (!is_a_skipped_site(cpu, ctx, return_segment, return_offset)) {
    // A step's first click, or a script's: the wait is what spaces them.
    return;
  }

  ctx.redirect(
      cs, static_cast<std::uint16_t>(ip + (epilogue_offset - bracket_offset)));
}

constexpr std::array<seam_point, 1> quick_steps_points{
    {{.module = resident_image,
      .offset = bracket_offset,
      .run = &skip_the_wait}}};

constexpr seam_definition quick_steps_definition{
    .id = "quick-steps",
    .about = "turn, and take the next step, without waiting for the click",
    .fingerprints = quick_steps_binaries,
    .points = quick_steps_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& quick_steps_seam() noexcept {
  return quick_steps_definition;
}

}  // namespace amberfolio::machine
