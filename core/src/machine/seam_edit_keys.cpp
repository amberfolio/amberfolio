// SPDX-License-Identifier: AGPL-3.0-only
//
// The edit-keys seam: an extended key (an arrow, Home, End, a page key, a
// function key) never reaches the program's line editor as a letter
// (#455).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and scan codes. Not one byte of the program
// is reproduced here.
//
// **The line editor** is one resident routine (`1709:09E3`, image
// `0x7A73`). It loops on the program's key read, which is a routine of the
// resident image's (`1899:0059`, image `0x89E9`) over the keyboard
// segment's read (`1A40:030F`, image `0xA70F`), and acts on the byte it
// returns: `0x20` to `0x7A` is appended and drawn, `0x08` is Backspace,
// `0x0D` and `0x1B` accept, and anything else is ignored.
//
// **An extended key comes back as two reads.** The keyboard read takes the
// BIOS word (scan code high, character low). When the character is zero it
// **keeps the scan code in a one-byte slot of the data segment** (`0x8501`)
// and answers zero. The next read finds the slot armed, empties it and
// answers the scan code. The editor ignores the zero, and then takes the
// scan code for a character: Right (`0x4D`) is `M`, Up (`0x48`) `H`, Down
// (`0x50`) `P`, Left (`0x4B`) `K`, Home `G`, End `O`, PgUp `I`, PgDn `Q`,
// Insert `R`, Delete `S`, and the function keys `;` to `D`. That is the
// original program on any PC.
//
// **Who calls the editor**, by two routes (the program's source, and a scan
// of the resident image and of every overlay for a far call to its entry,
// and of the resident segment's own near calls to it):
//
//   | caller | where | what it asks |
//   |---|---|---|
//   | the character's name at creation | overlay 16, call at `0x1E5A` | a name, fifteen letters |
//   | a script's free-text answer | overlay 3, call at `0x09C8` | a line, forty characters |
//   | a script's number prompt | the resident image, call at image `0x7C23`, from overlay 3's call at `0x097B` | up to six characters, then a number; a letter makes the program ask again |
//   | the copy-protection challenge's answer | overlay 2, call at `0x02FD` | six characters |
//
// There is no other: the icon editor and the save game's slot prompt do not
// type a line, and a save's name is the character's.
//
// **The program's other typed inputs.** The amount editor of the View >
// Drop money path reads its keys itself and accepts digits and Backspace
// only: an arrow's scan code is never a digit, so it is not wrong there.
// The one exception is an Alt-letter combination, whose scan codes `0x30` to
// `0x32` (B, N and M) are the digits 0 to 2; the BIOS delivers them as
// extended keys with no character. That is the same flaw at a much smaller
// scale, and in a routine of its own, so it is not covered here and the seam
// does not claim it (docs/seams.md §10).
//
//
// What the seam does
// ------------------
//
// **One point**, in the resident image, at the instruction after the
// editor's call into the program's key read (`1709:0A30`, image `0x7AC0`):
// the one that stores AL, the byte the editor is about to act on. The call is
// the five bytes before it. A handler there sees AL as the editor will.
//
// When AL is zero **and the slot is armed**, this is the first half of an
// extended key, and the handler empties the slot. The editor sees the zero it
// would have ignored anyway, goes round, and the next read takes a key from
// the keyboard as it does for every key it ignores. The scan code is never
// answered. Nothing is written but the byte the program was about to read and
// clear itself.
//
// **Why this and not the BIOS ring.** The ring is what `bar-keys` and
// `list-arrows` rewrite, because their points are before the read. The
// editor's loop has no poll: it goes straight into a blocking read, and a
// key that arrives while it is blocked is delivered without a point being
// reached (`seam_key_read.h`, which is why a claim at a read has to be
// answered). A point **after** the read sees every key the editor is handed,
// the one that arrived while it waited included, and takes nothing off the
// ring, so there is nothing to put back.
//
// **The scope is the editor's own call.** The point is inside the editor, so
// the program's other readers of the same key read (the menu-bar routine,
// every bar and list, the walk in 3D, combat) never reach it and an arrow
// is exactly what it was. The four callers above share it, and a seam on
// the editor covers all of them.
//
// **Checks.** The data segment is the one the facts name (`0xC7C`
// paragraphs after the image segment, the one every seam reads the program's
// data through); any other is not the frame these facts describe, and the
// handler declines and touches nothing.
//
// **The cost.** A key with no character is dropped at these prompts, so a
// function key or an Alt chord types nothing either, which is the point. The
// editor's own alphabet (`0x20` to `0x7A`, Backspace, Return, Esc) is all
// keys with a character, and none of them is touched.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On and no extended key typed at a line editor, the run is byte for byte
// the run with the seam off: the handler reads AL, and reads the slot only
// when AL is zero, and writes nothing unless the slot holds a scan code. The
// pair is an `identical` (a name typed in letters only) and a `contrast` (an
// arrow in the middle of it) (tests/sessions/README.md).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// Nothing outstanding. Out of scope: the money amount editor's Alt-letter
// digits (above), which is its own routine and not a line editor.

#include <array>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> edit_keys_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The editor ------------------------------------------------------------

/// In the resident image: the instruction after the line editor's call into
/// the program's key read, which stores AL. Offset from the image segment.
/// The editor's entry is `0x7A73`; the call is the five bytes before this.
constexpr std::uint32_t editor_after_read = 0x7AC0;

// --- The data segment ------------------------------------------------------

/// The data segment's distance from the image segment, in paragraphs.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;

/// The keyboard read's one-byte slot for an extended key's second half.
constexpr std::uint16_t data_key_pushback = 0x8501;

/// The editor has just been handed AL by the program's key read. If it is
/// the zero that starts an extended key, and the read has kept the scan code
/// for the next one, throw the scan code away.
void drop_the_scan_code(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  if (regs.get(cpu::reg8::al) != 0) {
    return;
  }
  const std::uint16_t ds = regs[cpu::sreg::ds];
  if (ds != static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                       dgroup_paragraphs)) {
    // Not the data segment the facts put the slot in.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (cpu.read_byte(ds, data_key_pushback) == 0) {
    return;
  }
  cpu.write_byte(ds, data_key_pushback, 0);
}

constexpr std::array<seam_point, 1> edit_keys_points{
    {{.module = resident_image,
      .offset = editor_after_read,
      .run = &drop_the_scan_code}}};

constexpr seam_definition edit_keys_definition{
    .id = "edit-keys",
    .about =
        "the arrows and other extended keys no longer type letters in the "
        "game's name and text prompts",
    .fingerprints = edit_keys_binaries,
    .points = edit_keys_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& edit_keys_seam() noexcept {
  return edit_keys_definition;
}

}  // namespace amberfolio::machine
