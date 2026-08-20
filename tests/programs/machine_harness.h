// SPDX-License-Identifier: AGPL-3.0-only
//
// The whole M2 machine, with a host loop around it, running a program to
// its own exit.
//
// harness.h next door is the M1 apparatus: a flat megabyte, no devices,
// and a loop that steps until HLT. It stays exactly as it is — the
// programs it runs pin the CPU-side baseline and feed `amberfolio-bench`,
// and a comparison is only a comparison while both sides of it exist.
// This file is the other end of the same axis: the PC memory map, the
// PIC, the PIT, the EGA and its renderer, the speaker, a memory
// filesystem, and the BIOS/DOS service floor with INT 10h and INT 21h
// installed on it. Everything M2 built, wired the way M2-H1 (#54) and
// M2-H2 (#55) will wire it, with nothing stubbed and nothing skipped.
//
//
// Why there are no knobs for which devices to attach
// ---------------------------------------------------
//
// Every machine here has all of them. A program that never programs the
// PIT is a program the PIT never interrupts; a program that never writes
// A000:0000 leaves the planes blank and the renderer composing black.
// Devices are not costs a program has to opt out of — they are the
// machine — and a harness with a wiring matrix would be testing seven
// different machines instead of the one this milestone exists to build.
// It would also be the wrong shape for #55, which embeds one of these
// programs in a dev page and wants exactly the machine a host builds.
//
//
// The host loop, and why it is a host loop
// -----------------------------------------
//
// `turn()` is `run()` to the next frame boundary, then present, then
// drain the console, then pull audio — the same four steps, in the same
// order, that platform.h's design essay prescribes and that
// tests/core/machine/test_host.h demonstrates against stand-in devices.
// This runs it against the real ones.
//
// It has to be a loop of slices rather than one long `run()` for two
// reasons that are both about the platform interface rather than about
// the machine: `audio_timeline`'s edge ring holds 2048 unconsumed edges
// and `console_output`'s ring holds 4096 unread bytes, and both drop
// what overflows rather than stall the machine (platform.h says why).
// A run that never pulled would lose the very output it was going to be
// asserted on. Slicing at the frame period is what a host does anyway,
// so the assertion surface below is the one a host actually sees.
//
//
// How a program ends
// -------------------
//
// Every machine program terminates through DOS: INT 21h AH=4Ch with a
// code in AL, or the PSP's own INT 20h. Both reach
// `machine::exit_program()` and stop the machine with
// `stop_reason::program_exited` and the code in `stop_record::exit_code`
// (diagnostics.h), so the harness's stop condition is one question —
// has the machine stopped — and the exit code is an answer every program
// gets to assert. HLT is not a termination here and is not treated as
// one: a halted machine is a machine waiting for an interrupt, which is
// exactly what the keyboard's blocking read and a timer wait look like
// from outside, and a harness that stopped on the first HLT could not
// run either of them.
//
//
// The result block
// -----------------
//
// A program's answers are 16-bit words at `machine_layout::result_offset`
// in its own segment, harvested after the run. One word per check, so a
// failure names which check failed instead of producing one opaque
// pass/fail bit — and because they are ordinary memory, a program
// computes them with ordinary instructions rather than needing a channel
// out of the machine that a real program would not have.
//
// GoogleTest is deliberately absent from this file and from every other
// file in this directory. That is what makes this apparatus build under
// Emscripten, which is what makes `ctest --preset wasm` run the
// interpreter rather than only compile it (tests/programs/CMakeLists.txt
// has the argument). The GoogleTest rig consumes this; it is never the
// other way around.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/speaker.h"

namespace amberfolio::programs {

/// Where a raw (non-EXE) machine program is loaded and what it points its
/// segment registers at, and where every program leaves its answers.
///
/// One segment for code, data and stack, which is what a real .COM-model
/// program has and what keeps a hand-written program's addressing
/// arithmetic to none. The segment is above the loader's own resident
/// floor (loader.h) and far below the video window, so a raw program and
/// a loaded one live at different addresses and neither can be mistaken
/// for the other in a dump.
struct machine_layout {
  static constexpr std::uint16_t code_segment = 0x1000;
  static constexpr std::uint16_t stack_pointer = 0xFFFE;

  /// The result block: `result_words` 16-bit answers, at this offset in
  /// the program's own segment. Past anything a hand-written program's
  /// code and scratch data occupy, and a round number so a program's
  /// stores read as what they are.
  static constexpr std::uint16_t result_offset = 0x0800;
};

/// The rate the harness pulls audio at.
///
/// 29,102 Hz, not 44,100: `pit_input_hz` divides by it exactly, so every
/// sample covers exactly 41 ticks and a sample index converts back to a
/// tick count with nothing to round. That is what lets the sound
/// program's measured tone periods be asserted as equalities rather than
/// as tolerance bands — speaker_test.cpp's own exit criterion makes the
/// identical choice for the identical reason.
inline constexpr unsigned audio_sample_rate = 29102;

/// Ticks in one sample at `audio_sample_rate`. Exact, by the choice
/// above.
inline constexpr machine::ticks ticks_per_sample =
    machine::pit_input_hz / audio_sample_rate;

static_assert(machine::pit_input_hz % audio_sample_rate == 0);

/// A file the memory filesystem starts with, or one it is expected to
/// hold afterwards.
struct staged_file {
  /// A raw DOS path, canonicalized on the way in — `\DATA\SAVE1.DAT`.
  std::string_view path;
  std::vector<std::uint8_t> contents;
};

/// A key event the host injects at a virtual time of its choosing.
///
/// The tick is the point: `machine::post_key()` timestamps an event with
/// the machine's own clock, so a scripted press has to be posted while
/// the machine is standing on the tick it should arrive at. The harness
/// posts each one at the first turn boundary at or past `at`, which is
/// exactly the granularity a real host's event pump has.
struct scripted_key {
  machine::ticks at{};
  std::uint8_t scancode{};
  machine::key_action action{machine::key_action::down};
};

/// Everything the harness needs to put a machine in front of one program.
struct machine_setup {
  /// A raw image, loaded at `machine_layout::code_segment` and entered at
  /// offset 0. Empty when `exe` is used instead; exactly one of the two
  /// is ever set.
  std::vector<std::uint8_t> code;

  /// An MZ file, written to the filesystem at `exe_path` and loaded
  /// through `load_program()` — the real DOS placement, the real PSP and
  /// the real relocations.
  std::vector<std::uint8_t> exe;
  std::string_view exe_path{"\\PROG.EXE"};

  /// Files the filesystem holds before the program runs.
  std::vector<staged_file> files;

  /// Key events, in the order they are posted. Need not be sorted; the
  /// harness posts each one once its tick has arrived.
  std::vector<scripted_key> keys;

  /// Paths whose contents the harness reads back afterwards, in this
  /// order, into `machine_outcome::files`. A path the run deleted comes
  /// back as `present == false` rather than as an error.
  std::vector<std::string_view> read_back;

  /// How many result words to harvest.
  std::size_t result_words{};

  /// What one scheduling step costs. One tick, unless a program says
  /// otherwise: every deadline in these programs then lands on the exact
  /// tick it was armed for, and a tone's edges fall on exact sample
  /// boundaries. The default governor's four ticks a step (clock.h) is
  /// the machine a player gets and the wrong machine to measure with.
  machine::ticks step_cost{1};

  /// Give up after this many scheduling steps. A program that never
  /// exits is a bug being reported, not a suite that never finishes.
  std::uint64_t step_cap{2'000'000};
};

/// A file the harness read back out of the filesystem after the run.
struct harvested_file {
  bool present{false};
  std::vector<std::uint8_t> contents;
};

/// What running a machine program did — everything a check could want to
/// look at, gathered once so that no reader has to drive the machine
/// itself to ask a second question.
struct machine_outcome {
  /// Scheduling steps, summed across every `machine::run()` slice.
  std::uint64_t steps{};

  /// Virtual time the machine reached. The number a "this took at least
  /// N ticks" claim is made against.
  machine::ticks time{};

  /// Wall time for the stepping loop alone, for the benchmark's table.
  double seconds{};

  machine::stop_record stop{};
  cpu::stop_record cpu_stop{};

  /// How the load went, for an `exe` program. `loader_error::none` for a
  /// raw one, which has no loader in its path at all.
  machine::loader_error load_error{machine::loader_error::none};
  machine::loaded_program loaded{};

  /// The result words, read out of the program's own segment.
  std::vector<std::uint16_t> results;

  /// Every console byte the run produced, in order.
  std::vector<std::uint8_t> console;

  /// Every audio sample pulled, at `audio_sample_rate`.
  std::vector<float> audio;

  /// The last frame the renderer completed, hashed, and how many it
  /// completed. Zero frames means the program exited inside the first
  /// frame period and the hash says nothing.
  std::uint64_t frame_hash{};
  std::uint64_t frames{};

  /// `machine_setup::read_back`, resolved.
  std::vector<harvested_file> files{};

  /// Everything the machine said nothing answers for, and everything a
  /// device refused. Both are assertions in their own right: a correct
  /// program touches no absent port and faults no device.
  std::uint64_t notices{};
  std::uint64_t device_stops{};

  /// How many times the program called the BIOS/DOS layer.
  std::uint64_t service_calls{};

  /// Audio pulls that ran out of settled virtual time. A host-pacing
  /// symptom (platform.h); here it should always be zero, because the
  /// harness pulls strictly less than it has run.
  std::uint64_t underruns{};

  /// The step cap ran out before the program exited.
  bool capped{false};

  /// The program exited through DOS, which is the only way a machine
  /// program is allowed to end.
  [[nodiscard]] bool exited() const noexcept {
    return stop.reason == machine::stop_reason::program_exited;
  }

  [[nodiscard]] std::uint8_t exit_code() const noexcept {
    return stop.exit_code;
  }
};

/// The machine, its devices, its filesystem and the host loop over them.
///
/// A class and not only a free function because #55's dev page wants to
/// drive the loop itself — one `turn()` per animation frame, reading the
/// framebuffer in between — and because a debugger wants the same. A
/// caller that only wants the answer calls `run_machine_setup()` below.
class machine_harness {
 public:
  /// Build the machine and wire everything to it. Nothing is loaded and
  /// nothing has run yet; `start()` does that.
  explicit machine_harness(const machine_setup& setup);

  machine_harness(const machine_harness&) = delete;
  machine_harness& operator=(const machine_harness&) = delete;
  machine_harness(machine_harness&&) = delete;
  machine_harness& operator=(machine_harness&&) = delete;
  ~machine_harness();

  [[nodiscard]] machine::machine& pc() noexcept { return *box_; }
  [[nodiscard]] machine::memory_filesystem& fs() noexcept { return *fs_; }

  /// Stage the files and put the program in memory: a raw image straight
  /// into RAM with the segment registers set, or an MZ file through the
  /// loader.
  ///
  /// False, and `outcome().load_error` says why, if the load failed.
  bool start();

  /// One turn of the host loop: post any key events now due, run to the
  /// next frame boundary, record a completed frame, drain the console,
  /// pull audio.
  ///
  /// False once the run is over — the machine stopped, or the step cap
  /// ran out.
  bool turn();

  /// Read the result words and the requested files back, and answer
  /// everything gathered. Safe to call more than once.
  [[nodiscard]] machine_outcome finish();

 private:
  /// The machine's single sink, kept rather than printed: what a stop was
  /// and how much of the machine was asked for things nothing answers
  /// for.
  class sink final : public machine::diagnostics {
   public:
    void report(const machine::notice& /*what*/) override { ++notices; }
    void report(const machine::service_call& /*call*/) override {
      ++service_calls;
    }
    void report(const machine::stop_record& /*stop*/) override {}
    void report(const cpu::stop_record& stop) override { cpu_stop = stop; }
    void report(const machine::device_stop& /*stop*/) override {
      ++device_stops;
    }

    cpu::stop_record cpu_stop{};
    std::uint64_t notices{};
    std::uint64_t device_stops{};
    std::uint64_t service_calls{};
  };

  /// Where the program's result block lives: its own segment for a raw
  /// image, the load segment for an EXE.
  [[nodiscard]] std::uint16_t result_segment() const noexcept;

  const machine_setup* setup_;

  sink log_;

  // Every one of these is on the heap: a machine carries a megabyte, an
  // EGA carries 256 KiB of planes, and a memory filesystem carries its
  // own store. None of them is a thing to put on a stack (machine.h,
  // ega.h, memory_vfs.h all say so).
  std::unique_ptr<machine::machine> box_;
  std::unique_ptr<machine::memory_filesystem> fs_;
  std::unique_ptr<machine::pic::controller> irq_;
  std::unique_ptr<machine::pit> timer_;
  std::unique_ptr<machine::speaker> sound_;
  std::unique_ptr<machine::ega> video_;
  std::unique_ptr<machine::renderer> screen_;

  machine_outcome result_{};

  /// The next frame boundary the loop runs to, and the generation the
  /// last recorded frame had — a host's own two variables.
  machine::ticks next_frame_{};
  std::uint64_t presented_{};

  /// How many of `setup_->keys` have been posted.
  std::size_t keys_posted_{};

  /// The underrun the priming pull in the constructor causes: it asks
  /// for a sample from a timeline whose horizon is still tick 0, which
  /// is by definition unsettled time. Counted here and subtracted in
  /// `finish()`, so that the number a program is judged on is the pulls
  /// its own run caused and not the one the harness needed.
  std::uint64_t primed_underruns_{};

  bool running_{true};
};

/// Build a machine, run the program to its exit, and answer what it did.
[[nodiscard]] machine_outcome run_machine_setup(const machine_setup& setup);

/// Canonicalize a raw DOS path the way a program's INT 21h call would.
/// Throws std::logic_error on a path that does not resolve — every path
/// in this directory is written here, so that is a mistake in a fixture
/// rather than a run-time condition.
[[nodiscard]] machine::dos_path parse_path(std::string_view raw);

/// FNV-1a over a frame's pixels and its palette. A host blits them; a
/// test needs one number, and the palette is part of what was drawn.
[[nodiscard]] std::uint64_t frame_hash(const machine::framebuffer& frame);

/// The virtual-time period of every tone in `samples`, in ticks.
///
/// A tone is a run of rising zero crossings with no silence between them;
/// a stretch where the output never crosses the threshold ends one and
/// begins the next. The period reported for each is the ticks between
/// its first and last rise divided by the cycles between them, which is
/// exact rather than averaged: `audio_timeline` box-filters the real edge
/// list, and `ticks_per_sample` carries no remainder, so the sample a
/// rise is detected in is the sample whose interval genuinely began the
/// high half of a cycle.
[[nodiscard]] std::vector<machine::ticks> tone_periods(
    std::span<const float> samples);

}  // namespace amberfolio::programs
