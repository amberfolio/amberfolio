// SPDX-License-Identifier: AGPL-3.0-only
//
// The code-wheel seam: PLAN.md §5 item 1, and the first entry of the
// build's seam table (seam_table.cpp).
//
// **It asks once.** Turn it on and the first launch is the machine's
// own: the challenge comes up exactly as it always did and this seam
// does nothing but watch. Answer it — off the cardboard wheel, off the
// manual, off the code generator application the releases sold today
// ship instead of a wheel — and the seam sees the program's own
// comparison come out equal, latches it, and tells the host, which
// remembers it. From the next launch the challenge is not drawn at all.
//
// That is M6-C1a (#291), and what it replaced is the possession gate
// M5-D3 built and #115 turned on: a fingerprint over a PDF of the wheel.
// The gate was never about the file — PLAN.md §5 says what it is for in
// one line, *it demonstrates the player holds the document, no more* —
// and a player who bought the game this year holds no such file. So the
// proof moved from the artifact to the act (#290). What did not move is
// the part worth keeping: **this seam does not answer the challenge for
// anybody.** There is no path through this build that gets past the
// wheel without a person having answered it once.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of this is addresses and offsets: facts about a binary, which the
// clean-content rule allows and which CONTRIBUTING.md names explicitly.
// Not one byte of the program is reproduced here, and nothing here
// describes its screens.
//
// **The challenge is answered by typing a word.** The program holds a
// table of thirteen candidate words in its resident data, as
// length-prefixed strings twenty-one bytes apart, and compares what was
// typed against the one the challenge selected. The comparison goes
// through the program's own general string-compare routine, which:
//
//   * loads both length-prefixed strings, leaving the two lengths in AL
//     (typed) and AH (expected) and the pointers at the characters;
//   * puts the *smaller* of the two lengths in CX;
//   * runs `REPE CMPSB` over that many characters;
//   * takes the branch after it if they differed, and otherwise compares
//     the two lengths;
//   * returns with the answer in ZF and nothing else.
//
// `compare_loop_offset` is that `REPE CMPSB`. It is in the resident image
// and cannot be overlaid, which is why the point is qualified by
// `resident_image` and nothing more (seam.h; overlay.h is for the points
// that are not).
//
// **The challenge is put on the screen by one far call**, in the boot's
// own tail, five bytes long, into the overlay stub the protection
// routine lives behind. `pool_of_radiance_challenge_call` is that call and
// `challenge_call_length` is how far past it the program continues.
// The routine behind it **sets no persistent state and returns on
// success**, which is the fact the skip below rests on: a program that
// never called it is a program that called it and got it right. The
// same call is what the program's own documented boot word skips, and
// this build deliberately does not use that word — it skips the titles
// as well and switches on the original's debug keys, which is three
// changes where a player asked for one.
//
//
// What the seam does, and what it is careful not to do
// -----------------------------------------------------
//
// **At the compare loop, while the challenge is unanswered, it reads and
// writes nothing.** The routine is the program's *general* string
// compare — the boot calls it roughly a hundred and fifty times before
// the challenge is even reached — so the qualifier is the expected
// operand: ES:DI must point inside the word table. Nothing but the
// code-wheel check compares against those words, and a comparison that
// does not is left completely alone. When it *is* the check, the handler
// performs the comparison the program is about to perform — the shorter
// length's worth of characters, then the two lengths — and if it comes
// out equal it latches the answer and calls the host. Not a register,
// not a byte, not a port is written on any path through it.
//
// That buys a claim this seam never had while it was answering the
// challenge for people: **on, and unanswered, it cannot move the
// machine at all**, so such a run is byte-for-byte the run with the seam
// off. `docs/seams.md` §7's invariant, holding for an *enabled* seam.
//
// **At the call, while the challenge is answered, it moves IP past it.**
// Five bytes, and the routine is never entered, so nothing it would have
// drawn is drawn. Before doing it the handler checks that the call is
// the call — the far target's own two words, against the stub's
// paragraph and offset — and declines rather than jumping if it is not
// (docs/seams.md §2: check what you can check). Addresses compared
// against addresses; no byte pattern is written down here and none is
// matched against.
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// **Nothing outstanding in the seam**, and saying so is the whole reason
// this section is here: an absent one and a satisfied one look identical,
// and this is the file §8.5 cites as the pattern (#272).
//
// What is left is not the seam's. **Nothing remembers the latch across a
// run yet** — that is #292, one host at a time, and until it lands the
// answer is remembered for exactly as long as the process lives. And the
// committed sessions still boot past a challenge this seam no longer
// answers, which is #293.

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// This seam's facts about one program (#555): the shared ones through
/// its profile (program_profile.h), and the ones only this seam reads.
/// One row per program it is available on; every handler is
/// instantiated on one row, so a handler never asks which program it is
/// on (the cheats' pattern, seam_cheats.cpp).
struct code_wheel_facts {
  const program_profile& profile;

  /// The far call into the protection routine: the overlay stub's own
  /// paragraph, counted from the image segment, and the entry's offset
  /// within it. The two words of the instruction's operand, and what the
  /// skip checks it is looking at before it jumps over anything.
  std::uint16_t challenge_stub_paragraph;
  std::uint16_t challenge_stub_offset;

  /// The `REPE CMPSB` inside the program's string-compare routine, as an
  /// offset from the image segment.
  std::uint32_t compare_loop_offset;

  /// The candidate-word table: length-prefixed entries, a stride apart,
  /// each `word_length` characters.
  std::uint32_t word_table_offset;
  std::uint32_t word_table_stride;
  std::uint32_t word_table_entries;
  std::uint32_t word_length;

  /// First and last character byte any entry's comparison can address:
  /// the span of *characters* the comparison can point at, first byte to
  /// last, because that span is the whole qualifier.
  [[nodiscard]] constexpr std::uint32_t word_chars_first() const noexcept {
    return word_table_offset + 1;
  }
  [[nodiscard]] constexpr std::uint32_t word_chars_last() const noexcept {
    return word_table_offset + (word_table_entries - 1) * word_table_stride +
           word_length;
  }

  /// Where the expected character is when the answer is one character
  /// (Curse of the Azure Bonds): a byte in the protection routine's own
  /// frame, this far below BP. Zero on a program whose answer is a word.
  std::uint16_t expected_below_bp;
};

/// Pool of Radiance: thirteen six-character words, twenty-one bytes apart,
/// and one call into the protection overlay, in the boot's own tail.
constexpr code_wheel_facts pool_of_radiance_code_wheel{
    .profile = pool_of_radiance,
    .challenge_stub_paragraph = 0x001C,
    .challenge_stub_offset = 0x0025,
    .compare_loop_offset = 0xBBB0,
    .word_table_offset = 0xC7C2,
    .word_table_stride = 21,
    .word_table_entries = 13,
    .word_length = 6,
    .expected_below_bp = 0};

/// The far call into the protection overlay, in the boot's own tail, as
/// an offset from the image segment. A far call is five bytes, and the
/// program goes on at the instruction after it.
constexpr std::uint16_t pool_of_radiance_challenge_call = 0x0122;
constexpr std::uint16_t challenge_call_length = 5;

/// Curse of the Azure Bonds (#555). The challenge is one character, read
/// off a translation wheel, and the routine that asks it lives in an
/// overlay of its own; the boot calls it twice (after the first title and
/// after the demonstration loop comes back) and the event-script
/// interpreter once, for a script's own protection command. All three are
/// the same five-byte far call into the routine's stub, at image `0x03F0`,
/// whose second entry is the routine. In the routine, the character typed
/// is in AL at a `CMP` against the expected one, kept three bytes below
/// BP, which is where the watch sits.
///
/// **The store copy accepts any answer**, the way Pool of Radiance's does
/// (docs/seams.md §5), and by its own branch: the conditional jump after
/// that compare goes on to the routine's exit when the two characters
/// *differ*. A right answer falls through to the program's refusal line,
/// and then to the exit as well. The watch does not care which way the
/// branch goes: it latches what the compare finds equal.
constexpr code_wheel_facts curse_code_wheel{.profile = curse_of_the_azure_bonds,
                                            .challenge_stub_paragraph = 0x003F,
                                            .challenge_stub_offset = 0x0025,
                                            .compare_loop_offset = 0x03C4,
                                            .word_table_offset = 0,
                                            .word_table_stride = 0,
                                            .word_table_entries = 0,
                                            .word_length = 0,
                                            .expected_below_bp = 3};

/// The overlay the protection routine lives in, on Curse: one routine and
/// its strings. The stub's record is at image `0x03F0`, so the
/// load-segment word is sixteen bytes in (program_profile.h).
constexpr seam_module curse_protection_overlay{
    .file = "GAME.OVR",
    .file_offset = 18133,
    .length = 1096,
    .digest =
        "b47fee60e71fe1b43ac45f98ffb93e66d2c89d40da970e5da4ee35e7d1f47969",
    .load_segment_at = 0x0400};

/// Curse's three calls into the challenge: the boot's two, in the
/// resident image, and the interpreter's, in its overlay.
constexpr std::uint16_t curse_first_challenge_call = 0x01F1;
constexpr std::uint16_t curse_demo_challenge_call = 0x0344;
constexpr std::uint16_t curse_script_challenge_call = 0x3263;

constexpr auto code_wheel_binaries =
    fingerprints_of(std::array{&pool_of_radiance, &curse_of_the_azure_bonds});

/// `offset + by`, wrapping in the segment as the program's own
/// addressing does.
[[nodiscard]] std::uint16_t at(std::uint16_t offset,
                               std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(offset + by);
}

/// The compare loop: watch for the player getting it right, and do
/// nothing else, ever.
template <const code_wheel_facts& F>
void watch_the_answer(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  // The expected operand, as a physical address, against the table this
  // seam's facts are about. `image_base()` is where the loader put the
  // program (seam.h): the offsets above are the program's, not this
  // machine's.
  const std::uint16_t es = regs[cpu::sreg::es];
  const std::uint16_t di = regs[cpu::reg16::di];
  const std::uint32_t expected = cpu::physical_address(es, di);
  const std::uint32_t base = ctx.image_base();
  if (expected < base + F.word_chars_first() ||
      expected > base + F.word_chars_last()) {
    // Some other string comparison, of which this program makes many.
    return;
  }
  if (box.seams().code_wheel_answered()) {
    // Already known, and there is nothing here to learn. A handler that
    // acted in this state would be a handler that answers the challenge
    // for somebody.
    return;
  }

  const std::uint16_t count = regs[cpu::reg16::cx];
  if (count > F.word_length) {
    // The routine puts the *smaller* length in CX and these words are
    // six characters, so a longer count is not the comparison this
    // seam's facts describe. Decline, and touch nothing.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint8_t typed_length = regs.get(cpu::reg8::al);
  const std::uint8_t expected_length = regs.get(cpu::reg8::ah);
  if (count == 0 || typed_length != expected_length) {
    // What the program's own length comparison is about to conclude.
    return;
  }

  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t si = regs[cpu::reg16::si];
  for (std::uint16_t i = 0; i < count; ++i) {
    // Through the bus, as the program — never `memory().ram()`
    // (docs/seams.md §3). Read only: this is the one seam that reads a
    // screen's worth of nothing and writes none of it.
    if (cpu.read_byte(ds, at(si, i)) != cpu.read_byte(es, at(di, i))) {
      return;
    }
  }

  // A person answered the program's own question, correctly. That is the
  // whole of what this enhancement ever wanted to know.
  box.seams().set_code_wheel_answered(true);
  static_cast<void>(ctx.call_host(seam_host_service::code_wheel_answered, 0));
}

/// Curse's compare: one character against one character. The same
/// contract as `watch_the_answer`: read, conclude what the program's own
/// compare concludes, and on an equal one latch and tell the host. Not a
/// register, not a byte is written.
template <const code_wheel_facts& F>
void watch_the_character(machine& box, seam_context& ctx) {
  if (box.seams().code_wheel_answered()) {
    return;
  }
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t bp = regs[cpu::reg16::bp];
  if (bp < F.expected_below_bp) {
    // A frame that cannot hold the routine's locals is not the frame
    // these facts describe.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint8_t typed = regs.get(cpu::reg8::al);
  const std::uint8_t expected =
      cpu.read_byte(regs[cpu::sreg::ss],
                    static_cast<std::uint16_t>(bp - F.expected_below_bp));
  if (typed != expected) {
    return;
  }
  box.seams().set_code_wheel_answered(true);
  static_cast<void>(ctx.call_host(seam_host_service::code_wheel_answered, 0));
}

/// The boot's call into the protection overlay: once the challenge has
/// been answered, go on at the instruction after it.
template <const code_wheel_facts& F>
void skip_the_challenge(machine& box, seam_context& ctx) {
  if (!box.seams().code_wheel_answered()) {
    // Not yet. The program calls its own routine and asks, which is the
    // machine's own behaviour and the point of the once.
    return;
  }

  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t cs = regs[cpu::sreg::cs];
  const std::uint16_t ip = regs.ip;

  // Check the call is the call. The two words of a far call's operand
  // are its offset and its segment, and both are known: the stub's own
  // offset, and its paragraph counted from wherever the loader put the
  // image — which is what the program's relocations left in that word.
  const std::uint16_t target_offset = cpu.read_word(cs, at(ip, 1));
  const std::uint16_t target_segment = cpu.read_word(cs, at(ip, 3));
  const auto image_paragraph =
      static_cast<std::uint16_t>(ctx.image_base() / 16U);
  if (target_offset != F.challenge_stub_offset ||
      target_segment != static_cast<std::uint16_t>(
                            image_paragraph + F.challenge_stub_paragraph)) {
    // Not the instruction these facts describe. Decline and touch
    // nothing: a seam that jumped five bytes over something else would
    // do its damage three layers from here.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  ctx.redirect(cs, at(ip, challenge_call_length));
}

/// Both points are in the resident image (seam.h: neither the compare
/// loop nor the boot's own tail can be overlaid), so both are qualified
/// by `resident_image` and both offsets are from the image segment.
constexpr std::array<seam_point, 2> pool_of_radiance_code_wheel_points{
    {{.module = resident_image,
      .offset = pool_of_radiance_code_wheel.compare_loop_offset,
      .run = &watch_the_answer<pool_of_radiance_code_wheel>},
     {.module = resident_image,
      .offset = pool_of_radiance_challenge_call,
      .run = &skip_the_challenge<pool_of_radiance_code_wheel>}}};

/// On Curse the watch is in the protection overlay, resolved through its
/// load-segment word; the boot's two calls are in the resident image, and
/// the script's is in the interpreter's overlay (the profile's).
constexpr std::array<seam_point, 4> curse_code_wheel_points{
    {{.module = curse_protection_overlay,
      .offset = curse_code_wheel.compare_loop_offset,
      .run = &watch_the_character<curse_code_wheel>},
     {.module = resident_image,
      .offset = curse_first_challenge_call,
      .run = &skip_the_challenge<curse_code_wheel>},
     {.module = resident_image,
      .offset = curse_demo_challenge_call,
      .run = &skip_the_challenge<curse_code_wheel>},
     {.module = curse_of_the_azure_bonds.interpreter_overlay,
      .offset = curse_script_challenge_call,
      .run = &skip_the_challenge<curse_code_wheel>}}};

/// The points on every program but the baseline, whose points are the
/// definition's own (seam.h, schema 9).
constexpr std::array<seam_program_points, 1> code_wheel_programs{
    {{.fingerprint = curse_of_the_azure_bonds.fingerprint,
      .points = curse_code_wheel_points}}};

constexpr seam_definition code_wheel_definition{
    .id = "code-wheel",
    .about = "answer the code-wheel challenge once, and never be asked again",
    .fingerprints = code_wheel_binaries,
    .points = pool_of_radiance_code_wheel_points,
    .programs = code_wheel_programs,
    // **No gate** (#290, #291). It had one — `document_kind::code_wheel`,
    // a fingerprint over a PDF of the wheel — from M5-D3 (#171) until the
    // releases sold today turned out to ship a code generator application
    // instead of that file. The mechanism is still there and still
    // documented (machine/document.h, docs/seams.md §5); no seam in this
    // build names it, and the code wheel's row in `known_documents()`
    // stays because a fingerprint of a file somebody hashed is a fact
    // whether or not anything gates on it.
    .schema = seam_schema_version};

}  // namespace

const seam_definition& code_wheel_seam() noexcept {
  return code_wheel_definition;
}

}  // namespace amberfolio::machine
