// SPDX-License-Identifier: AGPL-3.0-only
//
// The code-wheel seam, and the build's seam table it is an entry of.
//
// PLAN.md §5's first v1 enhancement, in the form M3 needs and no further.
// Read seam.h first — it says what a seam is, what this one is *not*
// (gated), and why any of it is here before M4.
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
// The challenge is answered by typing a word. The program holds a table
// of thirteen candidate words in its resident data, as length-prefixed
// strings twenty-one bytes apart, and compares what was typed against the
// one the challenge selected. The comparison goes through the program's
// own general string-compare routine, which:
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
//
// What the seam does, and what it is careful not to do
// -----------------------------------------------------
//
// The routine is the program's *general* string compare — the boot calls
// it roughly a hundred and fifty times before the gate is even reached —
// so firing on the address alone would corrupt every string comparison in
// the program. The qualifier is the expected operand: ES:DI must point
// inside the word table. Nothing but the code-wheel check compares
// against those words, and a comparison that does not is left completely
// alone.
//
// The surgery is then three registers and no memory at all:
//
//   * **CX = 0**, so the `REPE CMPSB` performs no iteration and therefore
//     changes no flag and moves neither pointer;
//   * **ZF set**, which is what the untaken branch after it wants to see;
//   * **AL = AH**, so the length comparison that follows agrees too.
//
// The program then runs its own code, reaches its own conclusion, and
// returns "equal" through its own convention. Nothing is written into the
// input buffer, so no assumption is made about how large that buffer is
// or what else shares the record it sits in — and the seam works whatever
// the player typed, including nothing.

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about — the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> code_wheel_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// The `REPE CMPSB` inside the program's string-compare routine, as an
/// offset from the image segment.
constexpr std::uint32_t compare_loop_offset = 0xBBB0;

/// The candidate-word table: thirteen length-prefixed six-character
/// entries, twenty-one bytes apart. Named as the span of *characters* the
/// comparison can point at, first byte to last, because that span is the
/// whole qualifier.
constexpr std::uint32_t word_table_offset = 0xC7C2;
constexpr std::uint32_t word_table_stride = 21;
constexpr std::uint32_t word_table_entries = 13;
constexpr std::uint32_t word_length = 6;

/// First and last character byte any entry's comparison can address.
constexpr std::uint32_t word_chars_first = word_table_offset + 1;
constexpr std::uint32_t word_chars_last =
    word_table_offset + (word_table_entries - 1) * word_table_stride +
    word_length;

void answer_the_code_wheel(machine& box, seam_context& ctx) {
  cpu::registers& regs = box.processor().regs();

  // The expected operand, as a physical address, against the table this
  // seam's facts are about. `image_base()` is where the loader put the
  // program (seam.h): the offsets above are the program's, not this
  // machine's.
  const std::uint32_t expected =
      cpu::physical_address(regs[cpu::sreg::es], regs[cpu::reg16::di]);
  const std::uint32_t base = ctx.image_base();
  if (expected < base + word_chars_first || expected > base + word_chars_last) {
    // Some other string comparison, of which this program makes many.
    return;
  }

  regs[cpu::reg16::cx] = 0;
  regs.set_flag(cpu::flag::zf, true);
  regs.set(cpu::reg8::al, regs.get(cpu::reg8::ah));
}

/// In the resident image (seam.h: the compare loop cannot be overlaid),
/// so the point is qualified by `resident_image` and the offset is from
/// the image segment.
constexpr std::array<seam_point, 1> code_wheel_points{
    {{.module = resident_image,
      .offset = compare_loop_offset,
      .run = &answer_the_code_wheel}}};

constexpr std::array<seam_definition, 1> seam_table{{
    {.id = "code-wheel",
     .about = "answer the code-wheel challenge (ungated; M5 owes the gate)",
     .fingerprints = code_wheel_binaries,
     .points = code_wheel_points,
     .schema = seam_schema_version},
}};

}  // namespace

std::span<const seam_definition> all_seams() { return seam_table; }

}  // namespace amberfolio::machine
