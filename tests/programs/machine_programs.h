// SPDX-License-Identifier: AGPL-3.0-only
//
// The self-written machine programs and what each of them is supposed to
// produce — M2's exit criterion (PLAN.md §7: "self-written real-mode test
// programs run correctly on all targets"), stated as data.
//
// programs.h next door is M1's list: three programs against a flat
// megabyte, asserting a register and a step count. This is the same idea
// one layer out. Each program here runs on the whole machine — CPU,
// PIC, PIT, EGA, renderer, speaker, filesystem, BIOS and DOS — and each
// asserts what the *machine* produced: result words the program computed
// and stored, its DOS exit code, the bytes it printed to the console, the
// tone periods measured back out of the pulled audio, the hash of the
// frame the renderer composed, and the files it left on the filesystem.
//
// One list, two readers, exactly as M1's is: the GoogleTest rig
// (tests/core/machine/machine_program_test.cpp) asserts these, and
// `amberfolio-bench` runs and times them — and on wasm the benchmark is
// the only reader there is, because the GoogleTest rig does not build
// under Emscripten. Nothing here may depend on GoogleTest; see
// machine_harness.h.
//
//
// Why a result block instead of registers
// -----------------------------------------
//
// M1's programs answer in a register, because a program that ends in HLT
// leaves its registers standing and one number is the whole answer. These
// programs end through DOS (INT 21h AH=4Ch), which is a real interrupt
// with a real handler behind it, and by the time the machine stops the
// register file belongs to whatever ran last. So each program writes its
// answers into memory as it goes — one 16-bit word per check, at
// `machine_layout::result_offset` — and the harness reads them back
// afterwards. The failure that produces names which check failed, which
// is worth more than the brevity of a register.
//
// Where a fact of interest is not a fixed number — how many polls a wait
// loop spun for, how many timer ticks accrued during a blocking read —
// the *program* reduces it to one, storing 1 or 0 for a condition it
// tested itself. That keeps every expectation an equality: a range check
// written in the checker would be a second, weaker language for saying
// what the program could just as well decide with a CMP.
//
//
// Where the bytes come from
// -------------------------
//
// The same place M1's do: written here, from the encoding, with the
// assembly listing above each builder and the bytes beneath it
// (programs.cpp's own convention). Nothing in this file, in the EXE
// fixtures it assembles, or in the files it stages on the virtual
// filesystem comes from anywhere but this repository — PLAN.md §6 and
// CONTRIBUTING.md's clean-content rule apply to test data exactly as they
// do to everything else.

#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/seam.h"
#include "programs/machine_harness.h"

namespace amberfolio::programs {

/// One word of a program's result block, and what it means. The name is
/// what a failure prints, so it says what was being checked rather than
/// restating the index.
struct expected_word {
  std::string_view what;
  std::uint16_t value{};
};

/// One pixel of the composed frame: where it is, and the palette index
/// it must hold.
///
/// The point of naming a pixel is that its value is *derivable*. A byte
/// written into the planes becomes eight pixels, MSB first, with plane
/// n contributing bit n of the index (renderer.h) — so a program that
/// knows what it put in each plane knows what colour every pixel it
/// touched came out. That is a fact about the hardware rules, worked out
/// by hand, and it is what a frame hash on its own can never be.
struct pixel_probe {
  unsigned x{};
  unsigned y{};
  std::uint8_t index{};
};

/// How many pixels of one palette index the whole frame holds.
///
/// The area a program filled, counted: a band of sixteen rows written
/// across the full width is 16 * 320 pixels of one colour and nothing
/// else is, so this catches a write landing at the wrong offset, a row
/// stride that is not 40 bytes, or a map mask reaching a plane it should
/// not — none of which a handful of sampled pixels need notice.
struct area_probe {
  std::uint8_t index{};
  std::size_t count{};
};

/// One entry of the composed frame's palette.
///
/// Also derivable by hand, and worth deriving: the EGA DAC gives each
/// channel a primary and a secondary bit and drives it to 00h, 55h, AAh
/// or FFh accordingly (ega.h), and the default sixteen codes INT 10h's
/// mode set installs are documented (int10.h). Between them, "palette
/// entry 9 is 55h/55h/FFh" is arithmetic rather than a golden.
struct palette_probe {
  unsigned index{};
  machine::rgb color{};
};

/// One machine program, everything it needs, and everything it must
/// produce.
struct machine_program {
  /// Identifies the CTest case, so: letters, digits and underscores.
  std::string_view name;

  /// One line, for the benchmark's table.
  std::string_view about;

  machine_setup setup;

  /// The result block, word by word. `setup.result_words` is set from
  /// this, so the two cannot disagree about how many there are.
  std::vector<expected_word> results;

  /// The code the program passes to INT 21h AH=4Ch. Distinct per program
  /// and never zero, so a program that exited by some other route — or
  /// one that fell into another program's exit path — is visible rather
  /// than plausible.
  std::uint8_t exit_code{};

  /// Every console byte the run should produce, in order. Asserted even
  /// when empty: a program that printed something it should not have is
  /// as wrong as one that printed the wrong thing.
  std::vector<std::uint8_t> console;

  /// The virtual-time period of every tone the run should play, in
  /// ticks. Asserted even when empty, for the reason `console` is.
  std::vector<machine::ticks> tone_periods;

  /// What `setup.read_back`'s paths should hold afterwards, in the same
  /// order. A path the program deleted is `present == false`.
  std::vector<harvested_file> files;

  /// Every naming DOS call the run should make, in order, rendered as
  /// `<action> <path>` - `open \OVL.BIN`, `close \OVL.BIN`
  /// (diagnostics.h's `file_event`, report.h's `file_action_name` and
  /// `format_dos_path`). Asserted even when empty, for the reason
  /// `console` is: a program that opened something it should not have is
  /// as wrong as one that opened the wrong thing.
  ///
  /// `files` above is what the filesystem held when the run ended; this
  /// is what the program did to get there, and the two answer different
  /// questions. A save game that creates a file, writes it and deletes a
  /// character file it replaced leaves a listing that cannot show the
  /// deletion happened at all.
  std::vector<std::string> file_trace;

  /// How many "something was asked of nothing" notices the run should
  /// produce (diagnostics.h). Zero for every program that is only
  /// exercising services — a correct program touches no absent port.
  ///
  /// Not zero for one that is exercising an *honest refusal*: the
  /// synthetic boot asks the video BIOS for 80x25 text, which this
  /// machine records and cannot draw, and then for the character under
  /// the cursor, which comes off a text page nothing answers for. Both
  /// notices are the point of those calls, so the count is asserted
  /// rather than forbidden — a run that stopped producing them would have
  /// started faking something.
  std::uint64_t notices{};

  /// Pixels, areas and palette entries of the composed frame, each
  /// worked out by hand from what the program wrote and from the rules
  /// in ega.h, renderer.h and int10.h. These are the claim about the
  /// picture; the hash below is only the backstop.
  std::vector<pixel_probe> pixels;
  std::vector<area_probe> areas;
  std::vector<palette_probe> palette;

  /// The hash of the last frame the renderer composed. Zero means the
  /// program draws nothing and the frame is whatever a blank machine
  /// shows, which is the renderer's business and not this program's —
  /// the two programs that draw assert it, and nothing else does.
  ///
  /// A golden, and it is only ever asserted *alongside* the probes
  /// above. On its own it would lock in whatever the machine happened to
  /// produce, bug included, and blame whoever later fixed it; underneath
  /// a set of checks that each say what the picture is, it is the thing
  /// that catches a change in the 63,900 pixels nobody named.
  std::uint64_t frame_hash{};

  /// The least virtual time the run can possibly have taken, computed
  /// from what the program asked the hardware for — a divisor times a
  /// tick count, say. Zero when there is nothing to compute it from.
  ///
  /// A lower bound and not an equality on purpose: it is the one claim
  /// that is a fact about the program's own arithmetic rather than about
  /// how many steps this emulator happened to take, and an equality here
  /// would be neither.
  machine::ticks least_time{};

  /// The least number of frames the renderer must have completed. A
  /// program whose framebuffer is asserted has to have run long enough
  /// for a frame boundary to fall after its last write, and this is what
  /// says so out loud.
  std::uint64_t least_frames{};

  /// What the run's seams must have asked of the host, per
  /// `machine::seam_host_service`, and what the last of each call
  /// carried (M5-D1, #169).
  ///
  /// Asserted always, and zero for every entry whose seams call out to
  /// nothing — for the reason `console` is asserted even when empty: a
  /// program that asked the host for something it should not have is as
  /// wrong as one that asked for the wrong thing. It is also the only
  /// way "the callout never reached anybody" is a claim at all, because
  /// nothing else this suite gathers can tell it from silence (#153).
  std::array<std::uint64_t, machine::seam_host_service_count>
      host_service_calls{};
  std::array<std::uint32_t, machine::seam_host_service_count>
      host_service_arguments{};

  /// The **exact** number of scheduling steps the run must take. Zero
  /// means no claim, which is what almost every entry here wants: how
  /// many steps this emulator takes is a fact about the emulator and
  /// pinning it would make every entry a golden.
  ///
  /// The exception is the one thing an exact step count is the right
  /// instrument for: **several entries claiming the same one**. The seam
  /// probes do (#163) — the plain machine, a trigger on and never
  /// pulled, and a point with no address on and never pulled — and that
  /// equality *is* the fidelity invariant (`machine/seam.h`), made where
  /// every target runs it rather than argued in a header. An entry that
  /// drifted off the shared number would be a seam that cost the machine
  /// something while switched off.
  std::uint64_t steps{};
};

/// Every machine program, in the order the benchmark should run them.
[[nodiscard]] std::vector<machine_program> all_machine_programs();

/// The seam the probe program (`seam_probe`) is written for: keyed to
/// that program's own fingerprint, with one point that edits a register
/// and one that posts a keystroke (M4-F2 #96's exit criterion, and the
/// seam the wasm smoke check toggles through the ABI for #98). A
/// function-local static, so it outlives any engine it is registered
/// with; the same definition every time, because the program is.
[[nodiscard]] const machine::seam_definition& seam_probe_definition();

/// The other half of the pair, and the one nothing about it is meant to
/// happen to: the same program's fingerprint, one point, and the point
/// sits on an instruction the program never executes — dead code the
/// assembler emits past the exit for exactly this purpose.
///
/// It is the shape of #131's failure, made on purpose: a seam that is
/// on, that arms, and that then does nothing, because its address is not
/// where execution goes. `armed` cannot tell it apart from a seam that
/// works and `fired == 0` can, which is what makes it the test the
/// counter exists for (#147). Its handler writes a sentinel over the
/// program's own first answer, so a run in which it *did* fire fails on
/// the result block too rather than only on the count.
[[nodiscard]] const machine::seam_definition& seam_probe_unreached_definition();

/// The third of the set (#161): the same program's fingerprint and the
/// same point as `probe`'s register edit, declared as a **trigger**, so
/// nothing happens there until somebody pulls it.
///
/// The pair of program entries it drives — `seam_probe_trigger` and
/// `seam_probe_trigger_unpulled` — is the fidelity claim at program
/// scale: on and asked, the result block carries the seam's word; on and
/// never asked, it carries the program's own, which is the same block a
/// machine with no seam at all produces.
[[nodiscard]] const machine::seam_definition& seam_probe_trigger_definition();

/// The fourth (#163): the same program's fingerprint, a trigger like the
/// one above, and a point with **no address** — offered at every step
/// boundary while the pull is outstanding rather than at an arrival.
///
/// Its handler is the shape such a handler has to have: it has no
/// address to tell it whether acting is safe, so it asks the machine,
/// declines while the answer is no — which keeps the latch — and acts at
/// the first step where the answer is yes. `seam_probe_pull` and
/// `seam_probe_pull_unpulled` drive it both ways, and the second shares
/// its exact step count with the plain machine's entry, which is the
/// fidelity invariant for a per-step guard: no pull, nothing consulted.
[[nodiscard]] const machine::seam_definition& seam_probe_pull_definition();

/// The fifth (M5-D1, #169): the same program's fingerprint, two points
/// on the same two instructions `probe` uses, and handlers that do
/// exactly one thing each — **call a host service**.
///
/// One asks for `journal_open`, the other for `automap_update`, and each
/// records what `call_host()` answered into a result word. That makes
/// the seam -> host direction a thing the whole apparatus drives on
/// every target, with no game binary anywhere near it, and it makes the
/// answer's two halves separable: `seam_probe_host` runs with a host
/// attached and expects both served, `seam_probe_host_unserved` runs
/// with none and expects both refused. The second is the one that
/// matters, because "the callout silently did nothing" is the failure
/// this door has, and a machine with no host is the only place it can
/// be produced deliberately.
///
/// Neither handler moves the machine, so both entries share the plain
/// machine's exact step count — a callout costs a run nothing.
[[nodiscard]] const machine::seam_definition& seam_probe_host_definition();

/// The build's own Encamp Fix (M5-E1, #172), keyed to the camp stand-in
/// program's fingerprint instead of the game's and registered under its
/// own id.
///
/// **The definition is copied, not written**: the points — and so the
/// handler, its guard and its arithmetic — are `all_seams()`'s, so what
/// the three `encamp_fix*` entries drive on all four targets is the same
/// function a player's copy would drive. Only the fingerprint differs,
/// because that is the field which decides what a set of addresses may be
/// applied to.
/// The test seam M5-D4's stand-in drives (#188): one point, whose handler
/// places a byte and queues two calls to a routine the program carries at
/// an offset the point's facts name. Unlike the camp entries this handler
/// is the *test's*, because what is being driven is the engine.
[[nodiscard]] const machine::seam_definition& seam_door_definition();

/// The call-door stand-in itself, as the MZ file those entries load.
[[nodiscard]] const std::vector<std::uint8_t>& seam_door_file();

[[nodiscard]] const machine::seam_definition& seam_camp_definition();

/// The camp stand-in itself, as the MZ file those entries load.
[[nodiscard]] const std::vector<std::uint8_t>& seam_camp_file();

/// The probe program itself, as the MZ file `seam_probe` loads — for a
/// host that wants to stage it somewhere of its own (hosts/web).
[[nodiscard]] const std::vector<std::uint8_t>& seam_probe_file();

/// The automap probe's image, and the seam that claims a key inside it
/// (M5-E2, #173).
[[nodiscard]] const std::vector<std::uint8_t>& automap_probe_file();
[[nodiscard]] const machine::seam_definition& automap_probe_definition();

/// One program by name, or null. The composite is `"composite"` — the
/// program M2-H2's dev page (#55) embeds, which is why finding one by
/// name is part of the interface rather than something a caller does with
/// a loop of its own.
[[nodiscard]] const machine_program* find_machine_program(
    std::string_view name);

/// Everything wrong with `got`, one line each, empty when it is correct.
///
/// A list of strings and not a stream of assertions because it has two
/// readers with different ideas of what a failure looks like: GoogleTest
/// wants them attached to a case, and the benchmark wants them printed.
/// Neither has its own copy of what "correct" means.
[[nodiscard]] std::vector<std::string> check_machine_program(
    const machine_program& expected, const machine_outcome& got);

/// Just the name — the same reasoning `PrintTo(const program&)` gives in
/// programs.h: GoogleTest would otherwise print a parameter by its bytes,
/// and gtest_discover_tests builds the CTest case name out of what this
/// prints.
void PrintTo(const machine_program& p, std::ostream* os);

}  // namespace amberfolio::programs
