// SPDX-License-Identifier: AGPL-3.0-only
//
// The SDL3 desktop host: one host for Windows, macOS and Linux
// (PLAN.md §4). It builds a machine, points it at a directory, loads a
// program, and gives it a screen, a speaker and a keyboard.
//
//     amberfolio <dir> <program.exe> [--headless] [--scale N]
//                                     [--verify] [--press KEY@FRAME]
//                                     [--steps N] [--until TICKS]
//                                     [--dump PREFIX] [--trace]
//                                     [--seam ID] [--seams] [--speed NAME]
//                                     [--fast N|max] [-- ARGUMENTS...]
//
// `--headless` opens no window and no audio device. That is what keeps
// the CI smoke test meaningful on a runner with neither, and it is the
// path M2-T1's host checks take.
//
// `--verify` and `--press` are the opposite: they exist so the *windowed*
// path can be run without a person in front of it. See "Checking the
// paths a headless run cannot" below.
//
// The rest arrived with M3-F1 (#83) and are the subject of the next
// section.
//
//
// The boot driver
// ---------------
//
// M3 boots the player's own copy, and the method (#94) is a loop: run it,
// read the line it stopped on, widen the one service that line names,
// run it again. Everything in this file that is not M2's host loop is in
// service of making that line worth reading.
//
//   --steps N        stop after N scheduling steps, wherever the program
//   --until TICKS    stop at TICKS of virtual time
//
//     A hang is otherwise the one failure this host cannot report: the
//     machine is running, nothing has refused anything, and the process
//     sits there. A budget turns it into an ending with a CS:IP, a step
//     count and a trace on it — which is a worklist entry, where a
//     hung process is not. The budget is clamped into the run slice
//     rather than checked after it, so the run ends on the step asked
//     for and not somewhere inside the frame after it: a stop you cannot
//     reproduce exactly is not a worklist entry either.
//
//   --dump PREFIX    write PREFIX.ppm and PREFIX.wav when the run ends
//
//     The frame the machine composed and the sound it made, in two
//     formats every viewer opens (dump.h). docs/machine.md §7's warning
//     about goldens is the argument: "the title renders" is a claim to
//     look at, and no test in this repository will ever run the file
//     that produces it.
//
//   --dump-every N   also write PREFIX-NNNNNN.ppm every N frames
//
//     A run is a film, and one frame of it is a still. Everything a
//     player-supplied copy does past the title happens over tens of
//     virtual seconds - a menu answers, a tour walks, a fight resolves -
//     and "what did the screen do" is a question a single final frame
//     cannot answer. Needs `--dump`, whose prefix it shares; the frame
//     number in the name is the same one `--press KEY@FRAME` counts in,
//     so a still and the keystroke that caused it are named in the same
//     units.
//
//     Deliberately every Nth frame rather than every frame: sixty files
//     a virtual second fills a disk before it tells anyone anything, and
//     the caller is the only one who knows how fast the thing they are
//     watching moves.
//
//   --trace          keep the trace ring, and print it with the report
//
//     Off by default, in the machine, at a cost of one branch per step
//     (machine/trace.h). What it answers is the question a bare address
//     cannot: how the program got there.
//
//     Since M4 (#97, #99) it also prints every file read the overlay
//     tracker records as it lands — the file, the offset, the length,
//     where it went and the digest of the bytes (machine/overlay.h):
//
//         amberfolio: overlay GAME.OVR offset=38919 length=4735 at=279D:0000 sha256=...
//
//     Those lines are the facts a seam qualified by an overlay is written
//     from, read off the program's own loads rather than inferred from
//     anything, and they are how the cheats seam's module was found.
//
//   --seam ID        turn on one seam, by its config key
//
//     PLAN.md §5's opt-in runtime patches, off unless named here — and
//     refused unless the program that was loaded is the one the seam's
//     addresses are facts about, which is what its fingerprint is for
//     (machine/seam.h). Repeatable. Every enabled seam is printed at
//     startup, because a run that had one on is not the same run as one
//     that did not and the log has to say so.
//
//     `code-wheel` is ungated, and seam.h is honest about what it is not
//     yet: the possession gate PLAN.md §5 requires is M5's, so today it
//     is a maintainer's switch on a maintainer's own copy.
//
//   --seams          list every seam this build carries, and exit
//
//     The toggle surface M4-F4 (#98) asks for: each seam's id, its
//     description, and where it stands against the program that was
//     loaded — off, on, or unavailable with the reason. Printed after
//     the load and after any `--seam` flags have been applied, so the
//     listing is the state the run would have started in, and then the
//     process exits 0 without running anything. The edition line it
//     prints beside the fingerprint is the other half of #95: which
//     known edition the file is, or that it is not one, in which case no
//     seam is available (machine/edition.h).
//
//   --speed NAME     which machine to be: xt, turbo, at or 386
//
//     The virtual clock's step cost (machine/clock.h), by the names the
//     presets already have. `xt` is the default and is the machine the
//     game was written for — a 4.77 MHz 8088 at about 298,000
//     instructions a second, which is slow enough to watch a title
//     screen paint itself line by line, because that is what an XT did.
//
//     The other three are not a fast-forward and not a hack: they are
//     the faster machines the same software ran on, and they change nothing
//     about what the emulator computes — virtual time still governs
//     every deadline, every tone and every tick, so a run at `at` is as
//     deterministic and as replayable as one at `xt`. What changes is
//     how much of it fits in a second of yours.
//
//     Which of them is *right* is a playtest question and not settled
//     here (#107, PLAN.md §9's note on pacing feel). This flag exists so
//     that the question can be asked by eye.
//
//   --fast N|max     run virtual time N times faster than the wall
//
//     The other way of going faster, and it is not the same way.
//     `--speed` changes *which machine this is*; this changes *how fast
//     you watch it*, and the difference is measurable rather than
//     philosophical. Booting the maintainer's copy splits into about 104
//     seconds of computation and about 21 seconds of pause the program
//     times against the BIOS tick. A faster processor divides the first
//     number and leaves the second alone, so twenty times the CPU is
//     still twenty-six seconds; fast-forward divides both, and twenty
//     times the wall rate is six.
//
//     Nothing inside the machine can tell. The step count, the tick
//     count, the frames composed and every byte of the framebuffer are
//     identical to a run at `--fast 1` — the only thing that changes is
//     how long this host sleeps at the bottom of the loop, which
//     platform.h's design essay is careful to keep outside machine state
//     for exactly this reason. That is what makes it safe to hand a
//     player a fast-forward before the replay harness exists (#100).
//
//     `max` does not sleep at all, and is what `--headless` has always
//     done — so the flag means nothing there and says so rather than
//     being quietly ignored. How fast `max` actually is depends on the
//     host: this interpreter runs about 22 million steps a second, which
//     at the default speed is roughly seventy times real time.
//
//     Audio is the one thing fast-forward spoils, unavoidably: the
//     speaker is pulled by a real 48 kHz device that cannot be hurried,
//     so anything past about 1x is producing sound faster than anything
//     can consume it. `--verify` counts the resyncs.
//
//   --record FILE    write this run down as a recording
//   --replay FILE    be the run a recording describes, and check it
//
//     The two halves of machine/replay.h, and the reason that file says
//     the player never runs the machine: this loop does. Recording adds
//     three things to it — a key line where a key is posted, a checkpoint
//     where a frame ends, an `end` line where the run does — and the
//     preamble, written before SDL is even up.
//
//     Replaying adds one thing and takes one away. It adds a clamp: the
//     slice stops at the recording's next event as readily as at a frame
//     boundary, because an event the machine *consumes* has to land on
//     the exact tick it was recorded at, and a loop that ran through the
//     tick first would be checking a machine that had already gone
//     somewhere else. The exception is a checkpoint of a stopped
//     machine, which this loop has to be allowed to run *past* to
//     arrive at, because stopping happens inside a step and spends
//     neither the step nor its ticks — machine/replay.h has that story.
//
//     It takes away the keyboard: the recording's keys are the run's
//     keys, and a key struck at the window during a replay is an input
//     the recorded run never had. The window still closes.
//
//     A replay does not take its speed or its seams from the command
//     line either — the recording named them, the player applies them
//     before it checks them, and `--speed`, `--seam` and `--press` are
//     refused alongside `--replay` rather than silently agreed with.
//
//     The verdict is the run's exit code, ahead of the program's own, on
//     the same reasoning as `--verify`: a run asked to check itself
//     against a recording is answering that question and not the
//     program's. Reaching the recording's `end` is part of passing.
//
//   -- ARGUMENTS     everything after `--` becomes the command tail
//
//     Passed to the loader verbatim, with the single leading space DOS's
//     own command-line parsing leaves in front of a tail. The PSP half of
//     this — what a program that parses its tail actually finds — is #89.
//
// The report itself is formatted in core, not here
// (machine/report.h), because M3's exit criterion is desktop *and* web
// and the two hosts have to print the same sentence at the same step for
// that comparison to mean anything (#84).
//
//
// The loop, and the one rule it exists to honour
// ----------------------------------------------
//
// PLAN.md §4: "host wall time only throttles presentation, outside
// machine state." So the loop is:
//
//     run the machine forward in *virtual* time to the next frame
//     boundary → present whatever frame that produced → sleep whatever
//     *wall* time is left over
//
// and never the other way round. If the host cannot keep up, the sleep
// is simply zero and presentation falls behind; virtual time is not
// slowed, not skipped, and not consulted about how long any of it took.
// A frame that was composed while the host was busy is dropped by the
// generation counter (platform.h) rather than delaying the machine.
//
// The corollary is that this host never asks the machine to catch up. A
// long stall on the host side does not become a burst of emulated
// instructions; the machine's clock is its own, and the only thing wall
// time decides is when we draw and how long we idle.
//
//
// Audio, and the thread that is allowed to touch it
// -------------------------------------------------
//
// `audio_timeline::render()` is the only function in the core that may be
// called off the machine thread, and by exactly one thread — not one at a
// time (platform.h states this contract). SDL's audio stream callback is
// that thread and the only place this file calls it. Everything else —
// `run()`, key posting, frame reads — happens on the main thread.
//
// An underrun is the host's problem: `render()` fills what it can and the
// rest is silence. Nothing back-pressures into machine state, because a
// machine that ran slower when the speaker was starved would no longer be
// deterministic, which is the whole point of the edge list being the
// canonical state rather than the samples.
//
//
// Checking the paths a headless run cannot
// ----------------------------------------
//
// Everything above the window — the machine, the VFS, the loader, the
// console, the exit code — is what `--headless` exercises and what CI has
// checked since M2-H1. Everything at the window was compiled and never
// run (#80): the texture upload, the integer scaling, the audio callback,
// and the step from an SDL key event to a posted XT scan code. "It
// compiles" is not the same claim as "it works", and the difference was
// due to be discovered by a game that has its own problems.
//
// So two options, and neither of them fakes anything:
//
//   --verify           after each frame is drawn and before it is
//                      presented, read the render target back and compare
//                      every pixel of it against the bytes this host
//                      uploaded. Count what the audio callback did on its
//                      own thread. Report all of it on stderr at exit,
//                      and fail the process if the picture did not match
//                      or if nothing was ever presented.
//
//   --press KEY@FRAME  push a real SDL keyboard event — down and up — into
//                      SDL's own queue at frame FRAME, so it comes back
//                      out of SDL_PollEvent and travels the same path a
//                      typed key does, mapping table included. KEY is
//                      whatever SDL_GetScancodeFromName accepts: `A`,
//                      `Escape`, `Left`, `Keypad 5`.
//
// Together they let one CTest case run the M2-T1 composite program in a
// real window, with a real audio device, on every desktop target — under
// SDL's `dummy` video and audio drivers, which are still the real SDL
// code paths, only pointed at no hardware. What that
// cannot check is the last inch: a photon leaving a display, a pressure
// wave leaving a speaker. docs/hosts.md says how a person checks those,
// and that is the part of #80 no runner can close.
//
// A note on the readback, since it is the load-bearing half. It happens
// *before* SDL_RenderPresent, because on an accelerated backend the
// contents of the back buffer after a present are undefined; before it,
// the target still holds what was drawn on every backend. And it derives
// its expectation rather than pinning a hash: each target pixel must
// equal the source pixel at (x/scale, y/scale), which is the definition
// of nearest-neighbour integer scaling and not a golden of whatever this
// machine happened to produce.
//
//
// What is deliberately not here
// -----------------------------
//
// Config file, onboarding, gamepad and the virtual keyboard are M6. The
// period-correct non-square-pixel option PLAN.md §4 lists is a `--scale`
// integer for now and an obvious place to grow an aspect mode; M4's
// polish is where that gets decided rather than guessed at here.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/speaker.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/machine/trace.h"
#include "amberfolio/sha256.h"
#include "amberfolio/version.h"
#include "directory_vfs.h"
#include "dump.h"
#include "keymap.h"

// <cstdio> rather than std::format/std::print, and not only for the wasm
// host's reason (bundle size). libc++ gates std::format's floating-point
// path behind macOS 13.3 availability, so *any* std::format call fails to
// compile against a deployment target of 11.0 — which is what the macos
// preset asks for. Revisit if that floor ever rises.

namespace {

using namespace amberfolio;

constexpr unsigned default_scale = 3;
constexpr unsigned audio_sample_rate = 48000;

/// How much of a run `--dump` keeps sound for, in seconds of virtual
/// time. Long enough to hear a title sequence through; short enough that
/// the buffer it reserves is measured in megabytes rather than
/// gigabytes, which matters because the audio thread appends to it and
/// so it can never be grown.
constexpr unsigned dump_audio_seconds = 60;

/// Everything the machine is made of, in one place so its construction
/// order is visible: the PIC exists before the PIT that raises IRQ0
/// through it, and the PIT before the speaker that gates channel 2.
struct wired_machine {
  explicit wired_machine(machine::diagnostics* log)
      : box(std::make_unique<machine::machine>(machine::memory_layout::pc,
                                               log)),
        irq(*box),
        timer(*box, irq),
        spk(*box, timer),
        video(std::make_unique<machine::ega>(*box)),
        render(*box, *video) {
    box->attach(irq);
    box->attach(timer);
    box->attach(spk);
    box->attach(*video);

    box->schedule(timer.channel0_deadline());
    box->schedule(timer.channel2_deadline());
    box->schedule(spk);
    box->schedule(render);

    machine::install_int10(box->services());
    machine::install_dos_services(box->services());

    box->reset();
    render.reset();
  }

  std::unique_ptr<machine::machine> box;
  machine::pic::controller irq;
  machine::pit timer;
  machine::speaker spk;
  std::unique_ptr<machine::ega> video;
  machine::renderer render;
};

/// A seam event's kind, in a word, for the log line the sink prints.
[[nodiscard]] const char* seam_event_name(
    machine::seam_event_kind kind) noexcept {
  switch (kind) {
    case machine::seam_event_kind::enabled:
      return "on";
    case machine::seam_event_kind::disabled:
      return "off";
    case machine::seam_event_kind::armed:
      return "armed";
    case machine::seam_event_kind::inert:
      return "inert";
    case machine::seam_event_kind::refused:
      return "refused";
  }
  return "unknown";
}

/// Reports what the core would not fake, to stderr. A host has to have
/// one of these or "log, don't fake" is only half a mechanism
/// (machine/diagnostics.h).
class stderr_diagnostics final : public machine::diagnostics {
 public:
  /// Whether to print every service call as it is made. Off by default,
  /// for the reason diagnostics.h gives: a call is something the program
  /// *did*, not a symptom of anything, and a boot makes tens of thousands
  /// of them. `--trace` turns it on, so that the live stream and the ring
  /// dumped at the end are one facility asked for once.
  void set_tracing(bool on) noexcept { tracing_ = on; }

  void report(const machine::notice& what) override {
    // Named rather than numbered (machine/report.h), and with the byte and
    // the caller it used to leave out. A number is a thing the reader has
    // to go and look up, on the one line whose whole purpose is to be
    // read.
    std::fprintf(stderr,
                 "amberfolio: notice %s at %05X value=%02X from=%04X:%04X\n",
                 machine::notice_kind_name(what.what), what.at, what.value,
                 what.cs, what.ip);
  }

  void report(const machine::stop_record& stop) override {
    // A program exiting is not a diagnostic. It is the run ending
    // the way it was asked to, and main() turns it into this
    // process's exit code; saying anything on stderr would make
    // every ordinary run look as though something had gone wrong.
    if (stop.reason == machine::stop_reason::program_exited) {
      return;
    }
    // Deliberately terse: the full account is the stop report main()
    // prints once the run has ended, and the same fact printed twice in
    // two shapes is how a reader comes to trust the wrong one.
    std::fprintf(stderr, "amberfolio: machine stopped, %s at %05X\n",
                 machine::stop_reason_name(stop.reason), stop.at);
  }

  void report(const cpu::stop_record& stop) override {
    std::fprintf(stderr,
                 "amberfolio: cpu stopped on opcode %02X at %04X:%04X\n",
                 stop.opcode, stop.cs, stop.ip);
  }

  void report(const machine::device_stop& stop) override {
    std::fprintf(
        stderr, "amberfolio: device declined %05X detail=%02X from=%04X:%04X\n",
        stop.at, stop.detail, stop.cs, stop.ip);
  }

  void report(const machine::seam_event& event) override {
    // Every transition, always: a seam going on, arming, or staying inert
    // is the one kind of line a boot log must never lose, because it is
    // the difference between a plain machine and an enhanced one
    // (machine/seam.h). Short, so the reason reads as the line.
    const bool why = event.reason != machine::seam_reason::none;
    std::fprintf(stderr, "amberfolio: seam %.*s %s%s%s\n",
                 static_cast<int>(event.id.size()), event.id.data(),
                 seam_event_name(event.kind), why ? " " : "",
                 why ? machine::seam_reason_name(event.reason) : "");
  }

  void report(const machine::file_event& event) override {
    if (!tracing_) {
      return;
    }
    std::array<char, machine::dos_path_capacity> path{};
    machine::format_dos_path(event.path, path);
    std::fprintf(
        stderr, "amberfolio: file %s %s handle=%04X %s from=%04X:%04X\n",
        machine::file_action_name(event.what), path.data(), event.handle,
        machine::vfs_error_name(event.error), event.caller_cs, event.caller_ip);
  }

  void report(const machine::service_call& call) override {
    if (!tracing_) {
      return;
    }
    std::fprintf(stderr, "amberfolio: call INT%02X ax=%04X from=%04X:%04X %s\n",
                 call.vector, call.ax, call.caller_cs, call.caller_ip,
                 call.outcome == machine::service_outcome::handled
                     ? "handled"
                     : "unimplemented");
  }

 private:
  bool tracing_{false};
};

/// Write one `--dump-every` still: `PREFIX-NNNNNN.ppm`, six digits so a
/// directory listing sorts into the order the frames happened in for any
/// run short of three virtual hours.
///
/// Failures are silent on purpose. A still is an observation aid, and a
/// run that stopped to complain about a full disk in the middle of the
/// thing being observed would have destroyed what it was there to show;
/// the missing file is the report.
void write_still(const std::string& prefix, std::uint64_t frame,
                 const machine::machine& box) {
  std::array<char, 32> suffix{};
  std::snprintf(suffix.data(), suffix.size(), "-%06llu.ppm",
                static_cast<unsigned long long>(frame));
  (void)sdl::write_ppm(std::filesystem::path(prefix + suffix.data()),
                       box.display().pixels(), box.display().palette());
}

/// Drain whatever DOS console output has accumulated to stdout.
///
/// Pulled, not pushed: `console_output` is a buffer the host empties, not
/// a sink the core writes through, because nothing in core ever calls out
/// (platform.h). There is no text mode to render to and none is planned,
/// so stdout is the whole of what a program's console output means here.
void drain_console(machine::machine& box) {
  std::array<std::uint8_t, 256> buffer{};
  for (;;) {
    const std::size_t got = box.console().read(buffer);
    if (got == 0) {
      return;
    }
    std::fwrite(buffer.data(), 1, got, stdout);
    // Flushed here rather than left to exit. A terminal would line-buffer
    // this and a pipe will not, so without it a program that echoes what
    // you type shows nothing at all until it ends - which is exactly the
    // shape of the check docs/hosts.md asks a person to make, and it
    // would look like the keyboard was dead.
    std::fflush(stdout);
  }
}

/// The audio callback's shared state. `box` is only ever read for its
/// `audio()`, and `render()` is the one core call the contract allows off
/// the machine thread.
///
/// The three counters are the only things the main thread reads back out,
/// and they are atomic for that reason alone: the audio thread writes
/// them, `--verify`'s report reads them once the stream is destroyed and
/// the callback can no longer be running. Relaxed ordering, because they
/// order nothing — they are a tally, not a handshake.
///
/// `sounded` is the one that says something the other two cannot. A
/// callback that ran and a buffer that was filled prove the plumbing;
/// they do not distinguish a speaker from a silence, because `render()`
/// answering silence is a correct answer to most of any run. Counting
/// the samples that were not zero is what tells a tone that reached
/// SDL's stream from a tone that was only ever in the edge list.
/// `capture` is `--dump`'s: a buffer sized once, before the stream is
/// opened, and filled by whichever thread does the pulling — the audio
/// callback when there is a device, the machine thread when there is
/// not. It is never grown while a callback might be running, which is
/// what makes appending to it from the audio thread legitimate; when it
/// is full it stops taking samples and `truncated` says so, rather than
/// allocating on the one thread that must not.
struct audio_bridge {
  machine::machine* box{};
  std::vector<float> scratch;
  std::atomic<std::uint64_t> callbacks{0};
  std::atomic<std::uint64_t> samples{0};
  std::atomic<std::uint64_t> sounded{0};
  std::vector<float> capture;
  std::atomic<std::size_t> captured{0};
  std::atomic<bool> truncated{false};
};

/// Append what was just pulled to the capture buffer, if there is one.
///
/// Called from the audio thread when a device is open and from the
/// machine thread when one is not; in both cases it is the *only* writer,
/// which is the whole of what the counters' relaxed ordering rests on
/// (the main thread reads them after the stream has been destroyed).
void capture_samples(audio_bridge& bridge, std::span<const float> pulled) {
  if (bridge.capture.empty()) {
    return;
  }
  const std::size_t at = bridge.captured.load(std::memory_order_relaxed);
  const std::size_t room = bridge.capture.size() - at;
  const std::size_t count = pulled.size() < room ? pulled.size() : room;
  for (std::size_t i = 0; i < count; ++i) {
    bridge.capture[at + i] = pulled[i];
  }
  bridge.captured.store(at + count, std::memory_order_relaxed);
  if (count < pulled.size()) {
    bridge.truncated.store(true, std::memory_order_relaxed);
  }
}

void SDLCALL feed_audio(void* userdata, SDL_AudioStream* stream, int additional,
                        int /*total*/) {
  auto* bridge = static_cast<audio_bridge*>(userdata);
  if (bridge == nullptr || bridge->box == nullptr || additional <= 0) {
    return;
  }

  const auto wanted = static_cast<std::size_t>(additional) / sizeof(float);
  if (bridge->scratch.size() < wanted) {
    // Grown on the audio thread, which is not ideal, but it happens once
    // per device-buffer size rather than per callback and the alternative
    // is guessing SDL's buffer size before it tells us.
    bridge->scratch.resize(wanted);
  }

  const std::span<float> out(bridge->scratch.data(), wanted);
  bridge->box->audio().render(out, audio_sample_rate);
  SDL_PutAudioStreamData(stream, out.data(),
                         static_cast<int>(wanted * sizeof(float)));

  std::uint64_t sounded = 0;
  for (const float sample : out) {
    if (sample != 0.0F) {
      ++sounded;
    }
  }

  bridge->callbacks.fetch_add(1, std::memory_order_relaxed);
  bridge->samples.fetch_add(wanted, std::memory_order_relaxed);
  bridge->sounded.fetch_add(sounded, std::memory_order_relaxed);

  capture_samples(*bridge, out);
}

/// A speed preset in words, for the line a non-default run prints.
[[nodiscard]] const char* speed_name(machine::speed_preset preset) noexcept {
  switch (preset) {
    case machine::speed_preset::pc_xt:
      return "xt (4.77 MHz 8088)";
    case machine::speed_preset::turbo_xt:
      return "turbo (8-10 MHz XT clone)";
    case machine::speed_preset::at:
      return "at";
    case machine::speed_preset::pc_386:
      return "386 (33 MHz 386DX)";
  }
  return "unknown";
}

/// Why a `--seam` was refused, in words. Named here rather than printed
/// as a number because the two that a person actually hits — the wrong
/// binary and a name that is not a seam — are the two a number would be
/// useless for.
[[nodiscard]] const char* seam_refusal(machine::seam_error why) noexcept {
  switch (why) {
    case machine::seam_error::none:
      return "no reason";
    case machine::seam_error::unknown_seam:
      return "no seam by that name";
    case machine::seam_error::wrong_binary:
      return "this seam's addresses are facts about a different binary";
    case machine::seam_error::no_program:
      return "no program was loaded to key it on";
    case machine::seam_error::schema_mismatch:
      return "this seam was written against another schema version";
    case machine::seam_error::module_not_resident:
      return "the module this seam lives in is not resident";
    case machine::seam_error::point_not_recognized:
      // Never an answer to `enable()` - a handler produces it, at a
      // point, and the host renders it through the seam-event line. Here
      // because the enumeration is one and a switch over it has to be
      // whole.
      return "what is at one of this seam's points is not what its facts"
             " describe";
    case machine::seam_error::too_many_points:
      return "too many interception points for this build";
    case machine::seam_error::no_room:
      return "the seam registry is full";
  }
  return "unknown";
}

/// A keystroke the host gives itself: which key, and which frame of the
/// loop to push it on.
///
/// The key is kept as SDL's own name until SDL is up, because
/// `SDL_GetScancodeFromName` is a question about SDL's tables and asking
/// it before SDL_Init is asking it early. Frame numbers count iterations
/// of the loop below, which are virtual frame periods — the same unit
/// machine_harness.h's `scripted_key` counts in, one layer further out.
struct scripted_press {
  std::string key;
  std::uint64_t frame{};
  SDL_Scancode code{SDL_SCANCODE_UNKNOWN};
  bool done{false};
};

/// Everything `--verify` has to say at the end of a run.
struct verify_report {
  std::uint64_t composed{};    ///< Frames the renderer finished.
  std::uint64_t presented{};   ///< Frames this host uploaded and presented.
  std::uint64_t checked{};     ///< Presented frames read back and compared.
  std::uint64_t mismatched{};  ///< Pixels that came back wrong, in total.
  std::uint64_t unreadable{};  ///< Presents whose target would not read back.
  std::uint64_t odd_size{};    ///< Presents whose target was not a whole
                               ///< multiple of the frame (a HiDPI backing
                               ///< store, say) and so was not compared.
  std::uint64_t keys{};        ///< Key events this host posted to the machine.
};

/// Push one SDL key event, as though a keyboard had sent it.
///
/// `SDL_PushEvent` puts it on the same queue a device driver's events go
/// on, so it comes back out of the `SDL_PollEvent` loop below and is
/// mapped, filtered and posted by exactly the code a typed key meets.
/// An event this host synthesized and then handled itself would prove
/// nothing about that code, which is the whole point.
void push_key_event(SDL_Window* window, SDL_Scancode code, bool down) {
  SDL_Event event{};
  event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.timestamp = SDL_GetTicksNS();
  event.key.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
  event.key.scancode = code;
  event.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
  event.key.mod = SDL_KMOD_NONE;
  event.key.down = down;
  event.key.repeat = false;
  SDL_PushEvent(&event);
}

/// Read the render target back and compare it, pixel for pixel, with the
/// buffer this host uploaded.
///
/// The expectation is derived, not stored: nearest-neighbour scaling by
/// an integer factor means target pixel (x, y) is source pixel
/// (x / scale, y / scale), and nothing else. So this checks the upload,
/// the scaling and the draw in one pass over the whole target, without a
/// golden anywhere — a wrong stride, a swapped colour channel, a texture
/// that never got the new frame and a scale that is not integer all show
/// up as a mismatch count rather than as a picture nobody looked at.
///
/// Called before `SDL_RenderPresent`: after it, an accelerated backend's
/// back buffer holds whatever the driver left there.
void verify_target(SDL_Renderer* renderer, std::span<const std::uint32_t> src,
                   verify_report& report) {
  SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
  if (shot == nullptr) {
    ++report.unreadable;
    return;
  }

  SDL_Surface* rgb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888);
  SDL_DestroySurface(shot);
  if (rgb == nullptr) {
    ++report.unreadable;
    return;
  }

  // Whole multiples only. A HiDPI backing store makes the target a larger
  // multiple than `--scale` asked for, which is still exact and still
  // checkable; anything that is not a multiple at all is a target this
  // function has no derivation for, and it says so rather than guessing.
  const auto width = static_cast<unsigned>(rgb->w);
  const auto height = static_cast<unsigned>(rgb->h);
  const unsigned scale_x = width / machine::frame_width;
  const unsigned scale_y = height / machine::frame_height;
  if (scale_x == 0 || scale_y == 0 || width != machine::frame_width * scale_x ||
      height != machine::frame_height * scale_y) {
    ++report.odd_size;
    SDL_DestroySurface(rgb);
    return;
  }

  const bool lock = SDL_MUSTLOCK(rgb);
  if (lock && !SDL_LockSurface(rgb)) {
    ++report.unreadable;
    SDL_DestroySurface(rgb);
    return;
  }

  const auto* base = static_cast<const std::uint8_t*>(rgb->pixels);
  const auto pitch = static_cast<std::size_t>(rgb->pitch);
  std::uint64_t wrong = 0;
  for (unsigned y = 0; y < height; ++y) {
    const auto* row = reinterpret_cast<const std::uint32_t*>(base + y * pitch);
    const std::size_t source_row =
        static_cast<std::size_t>(y / scale_y) * machine::frame_width;
    for (unsigned x = 0; x < width; ++x) {
      // The top eight bits are the X of XRGB8888 and belong to nobody.
      if ((row[x] & 0x00FFFFFFU) != src[source_row + (x / scale_x)]) {
        ++wrong;
      }
    }
  }

  if (lock) {
    SDL_UnlockSurface(rgb);
  }
  SDL_DestroySurface(rgb);

  ++report.checked;
  report.mismatched += wrong;
}

/// Print every overlay-tracker record newer than `printed`, and move it
/// on (machine/overlay.h). Once per slice, so a read that was replaced
/// inside one slice is not seen — a trace, not a log — which is plenty
/// for the thing it is for: reading the facts of a load off the program
/// rather than guessing them.
void print_overlay_loads(const machine::machine& box, std::uint64_t& printed) {
  const machine::overlay_tracker& overlays = box.overlays();
  std::uint64_t newest = printed;
  for (std::size_t i = 0; i < overlays.count(); ++i) {
    const machine::overlay_load& load = overlays.at(i);
    if (load.generation <= printed) {
      continue;
    }
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(load.digest, hex));
    const std::span<const char> name = load.file.leaf().text();
    std::fprintf(stderr,
                 "amberfolio: overlay %.*s offset=%u length=%u at=%04X:%04X"
                 " sha256=%s\n",
                 static_cast<int>(name.size()), name.data(), load.file_offset,
                 load.length, load.segment, load.offset, hex.data());
    if (load.generation > newest) {
      newest = load.generation;
    }
  }
  printed = newest;
}

/// Where this run slice has to stop: the next frame boundary, or a
/// budget, whichever comes first.
///
/// Clamping the slice rather than checking after it is what makes
/// `--steps N` end on step N rather than somewhere inside frame N+1. A
/// step budget becomes a tick budget through
/// `machine::time_after_steps()`, which is the machine's own arithmetic
/// because it is the only thing that knows the fraction of a tick
/// carried over from the last step — on a machine faster than one
/// instruction per tick, doing the multiplication out here would land a
/// tick away from the step actually asked for.
///
/// A replay's next event is one more thing that may bring it closer, and
/// the reason it is a clamp and not a check: an event the machine
/// consumes has to arrive on the exact tick it was recorded at, and a
/// loop that noticed the tick after running through it would already
/// have run the wrong machine. The one thing the player does not answer
/// here is a checkpoint of a stopped machine, which this loop has to be
/// allowed to run past in order to arrive at (machine/replay.h).
[[nodiscard]] machine::ticks slice_end(const machine::machine& box,
                                       machine::ticks frame_ticks,
                                       std::uint64_t step_budget,
                                       machine::ticks tick_budget,
                                       machine::ticks next_event) {
  machine::ticks target = box.time() + frame_ticks;

  if (tick_budget != 0 && tick_budget < target) {
    target = tick_budget;
  }

  if (next_event < target) {
    target = next_event;
  }

  if (step_budget != 0 && box.steps() < step_budget) {
    // Saturating, so a budget too big for the clock leaves the frame
    // boundary alone — which is right, because a run cannot reach it.
    const machine::ticks by_steps =
        box.time_after_steps(step_budget - box.steps());
    if (by_steps < target) {
      target = by_steps;
    }
  }

  return target;
}

struct options {
  std::filesystem::path root;
  std::string program;
  bool headless{false};
  unsigned scale{default_scale};
  bool verify{false};
  std::vector<scripted_press> presses;
  std::vector<std::string> seams;
  bool list_seams{false};
  machine::speed_preset speed{machine::default_speed};

  /// How many seconds of virtual time to run per second of wall time.
  /// Zero means "do not pace at all" — `--fast max`, and what
  /// `--headless` does regardless.
  double fast{1.0};

  /// Zero means "no budget" for both. Zero is not a budget anyone can
  /// want — a run of no steps observes nothing — so it is free to be the
  /// sentinel, and a caller does not have to say `--steps 0` to mean
  /// "unlimited".
  std::uint64_t step_budget{0};
  machine::ticks tick_budget{0};

  /// `--dump-every`: write a still every this many frames, on top of
  /// the one `--dump` writes at the end. Zero means "only the last
  /// one", which is what `--dump` alone has always meant.
  std::uint64_t dump_every{0};

  /// Where `--record` writes the recording, and where `--replay` reads
  /// one. Empty when the option was not given, and never both at once:
  /// a run is either the one being recorded or the one being checked
  /// against a recording, and one that tried to be both would be
  /// recording its own checks.
  std::string record_path;
  std::string replay_path;

  /// Where `--dump` writes. Empty when it was not asked for; the two
  /// files are this plus `.ppm` and `.wav`.
  std::string dump_prefix;

  bool trace{false};

  /// Everything after `--`, joined with single spaces and with the one
  /// leading space DOS leaves in front of a command tail. Empty when
  /// there was no `--`, which is a program invoked with no arguments
  /// rather than one invoked with an empty argument.
  std::string command_tail;

  bool valid{false};
};

/// A non-negative integer argument, or false for anything that is not
/// one. `strtoull` rather than `atoi` for the reason `--scale` already
/// gives: it can tell "0" from "not a number", and here the difference
/// decides whether a budget exists at all.
[[nodiscard]] bool parse_count(const char* text, std::uint64_t& out) {
  if (text == nullptr || *text == '\0' || *text == '-') {
    return false;
  }
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  out = static_cast<std::uint64_t>(value);
  return true;
}

/// `KEY@FRAME`, into a press. False on anything that is not that.
///
/// Split on the *last* `@`, because SDL names a key by the legend printed
/// on it and some legends are punctuation. There is no `@` key on a US
/// board, but splitting on the last one costs nothing and stops that from
/// being a fact this parser quietly depends on.
[[nodiscard]] bool parse_press(std::string_view spec, scripted_press& out) {
  const std::size_t at = spec.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == spec.size()) {
    return false;
  }
  const std::string_view digits = spec.substr(at + 1);
  std::uint64_t frame = 0;
  // Both ends named before the call. `from_chars` is given the length —
  // that is what the second pointer is — but clang-tidy reads a bare
  // `.data()` in an argument list as a string handed over without one,
  // and it is right to, often enough that hoisting is cheaper than an
  // exemption.
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed = std::from_chars(first, last, frame);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.key = std::string(spec.substr(0, at));
  out.frame = frame;
  return true;
}

[[nodiscard]] options parse(int argc, char** argv) {
  options opts;
  std::vector<std::string_view> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--") {
      // Everything past here belongs to the program, not to this host —
      // including anything that looks like one of our own options, which
      // is the entire point of the separator. The leading space is what
      // COMMAND.COM leaves between the program name and its tail, and a
      // program that counts characters at PSP:80h expects it.
      for (int j = i + 1; j < argc; ++j) {
        opts.command_tail += ' ';
        opts.command_tail += argv[j];
      }
      break;
    }
    if (arg == "--headless") {
      opts.headless = true;
    } else if (arg == "--verify") {
      opts.verify = true;
    } else if (arg == "--press" && i + 1 < argc) {
      scripted_press press;
      if (!parse_press(argv[++i], press)) {
        std::fprintf(stderr,
                     "amberfolio: --press wants KEY@FRAME, as in A@60\n");
        return opts;
      }
      opts.presses.push_back(std::move(press));
    } else if (arg == "--seam" && i + 1 < argc) {
      opts.seams.emplace_back(argv[++i]);
    } else if (arg == "--seams") {
      opts.list_seams = true;
    } else if (arg == "--trace") {
      opts.trace = true;
    } else if (arg == "--dump" && i + 1 < argc) {
      opts.dump_prefix = argv[++i];
    } else if (arg == "--dump-every" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.dump_every) || opts.dump_every == 0) {
        std::fprintf(stderr,
                     "amberfolio: --dump-every wants a positive frame "
                     "count\n");
        return opts;
      }
    } else if (arg == "--record" && i + 1 < argc) {
      opts.record_path = argv[++i];
    } else if (arg == "--replay" && i + 1 < argc) {
      opts.replay_path = argv[++i];
    } else if (arg == "--steps" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.step_budget) || opts.step_budget == 0) {
        std::fprintf(stderr, "amberfolio: --steps wants a positive count\n");
        return opts;
      }
    } else if (arg == "--until" && i + 1 < argc) {
      std::uint64_t ticks = 0;
      if (!parse_count(argv[++i], ticks) || ticks == 0) {
        std::fprintf(stderr, "amberfolio: --until wants a positive tick\n");
        return opts;
      }
      opts.tick_budget = static_cast<machine::ticks>(ticks);
    } else if (arg == "--fast" && i + 1 < argc) {
      const std::string_view rate = argv[++i];
      if (rate == "max") {
        opts.fast = 0.0;
      } else {
        char* end = nullptr;
        const double value = std::strtod(std::string(rate).c_str(), &end);
        if (end == nullptr || *end != '\0' || !(value > 0.0)) {
          std::fprintf(stderr,
                       "amberfolio: --fast wants a positive number, or max\n");
          return opts;
        }
        opts.fast = value;
      }
    } else if (arg == "--speed" && i + 1 < argc) {
      const std::string_view name = argv[++i];
      if (name == "xt") {
        opts.speed = machine::speed_preset::pc_xt;
      } else if (name == "turbo") {
        opts.speed = machine::speed_preset::turbo_xt;
      } else if (name == "at") {
        opts.speed = machine::speed_preset::at;
      } else if (name == "386") {
        opts.speed = machine::speed_preset::pc_386;
      } else {
        std::fprintf(stderr,
                     "amberfolio: --speed wants xt, turbo, at or 386\n");
        return opts;
      }
    } else if (arg == "--scale" && i + 1 < argc) {
      // strtol rather than atoi, which cannot tell "0" from "not a
      // number" - a distinction worth having when the answer decides
      // how big a window is.
      char* end = nullptr;
      const long value = std::strtol(argv[++i], &end, 10);
      opts.scale = (end != nullptr && *end == '\0' && value > 0)
                       ? static_cast<unsigned>(value)
                       : default_scale;
    } else if (arg.starts_with("--")) {
      std::fprintf(stderr, "amberfolio: unknown option %.*s\n",
                   static_cast<int>(arg.size()), arg.data());
      return opts;
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.size() != 2) {
    std::fprintf(
        stderr,
        "usage: amberfolio <dir> <program.exe> [--headless]"
        " [--scale N] [--verify] [--press KEY@FRAME]\n"
        "                                      [--steps N]"
        " [--until TICKS] [--dump PREFIX] [--dump-every N]\n"
        "                                      [--trace]\n"
        "                                      [--seam ID] [--seams]\n"
        "                                      [--record FILE]"
        " [--replay FILE]\n"
        "                                      [--speed xt|turbo|at|386]\n"
        "                                      [--fast N|max]\n"
        "                                      [-- ARGUMENTS...]\n");
    return opts;
  }

  // Both diagnostics need the window and the event queue that
  // `--headless` is defined as not opening. Refused rather than
  // quietly ignored: a check that reports nothing because its own
  // arguments cancelled out is worse than one that never ran.
  // `--headless` never sleeps, so it is already running as fast as this
  // machine can be run. Saying so beats accepting a number that would
  // change nothing.
  if (opts.headless && opts.fast != 1.0) {
    std::fprintf(stderr,
                 "amberfolio: --fast needs a window; --headless already"
                 " runs unpaced\n");
    return opts;
  }

  if (opts.headless && (opts.verify || !opts.presses.empty())) {
    std::fprintf(stderr,
                 "amberfolio: --verify and --press need a window;"
                 " they cannot be combined with --headless\n");
    return opts;
  }
  // The stills share `--dump`'s prefix, so without one there is nowhere
  // to put them. Refused for the reason above: an option that silently
  // did nothing is worse than one that says why it cannot.
  if (opts.dump_every != 0 && opts.dump_prefix.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --dump-every needs --dump, whose prefix it"
                 " writes under\n");
    return opts;
  }

  // A run records or it replays; it does not do both. The recording of a
  // replay would be a copy of its own input with the checks folded in,
  // and a file that is neither the run nor the verification of one.
  if (!opts.record_path.empty() && !opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --record and --replay are the two halves of"
                 " one thing; ask for one\n");
    return opts;
  }

  // The recording names the speed, the seams and every key, and a player
  // applies all three before it checks them (machine/replay.h). A command
  // line that also named one of them would be either agreeing silently or
  // disagreeing silently, and the second is a divergence reported as a
  // mismatched initial condition — true, but three steps from the cause.
  // Said here instead.
  if (!opts.replay_path.empty()) {
    const char* also = nullptr;
    if (!opts.seams.empty()) {
      also = "--seam";
    } else if (opts.speed != machine::default_speed) {
      also = "--speed";
    } else if (!opts.presses.empty()) {
      also = "--press";
    }
    if (also != nullptr) {
      std::fprintf(stderr,
                   "amberfolio: the recording decides the seams, the speed"
                   " and the keys; %s cannot be given with --replay\n",
                   also);
      return opts;
    }
  }

  opts.root = std::filesystem::path(positional[0]);
  opts.program = std::string(positional[1]);
  opts.valid = true;
  return opts;
}

}  // namespace

int main(int argc, char** argv) try {
  const options opts = parse(argc, argv);
  if (!opts.valid) {
    return EXIT_FAILURE;
  }

  sdl::directory_filesystem files(opts.root);
  if (!files.usable()) {
    std::fprintf(stderr, "amberfolio: %s is not a directory\n",
                 opts.root.string().c_str());
    return EXIT_FAILURE;
  }

  stderr_diagnostics log;
  wired_machine wired(&log);
  machine::machine& box = *wired.box;
  // The machine to be, before anything runs (machine/clock.h). Printed
  // whenever it is not the default, for the reason a seam is: a run at a
  // speed nobody expected is a different run, and a log that did not say
  // so would be describing the wrong machine.
  box.set_speed(opts.speed);
  if (opts.speed != machine::default_speed) {
    std::fprintf(
        stderr, "amberfolio: speed %s, about %llu steps a second\n",
        speed_name(opts.speed),
        static_cast<unsigned long long>(machine::steps_per_second(opts.speed)));
  }

  if (opts.fast == 0.0) {
    std::fprintf(stderr, "amberfolio: fast-forward unpaced\n");
  } else if (opts.fast != 1.0) {
    std::fprintf(stderr, "amberfolio: fast-forward %gx wall time\n", opts.fast);
  }

  box.set_filesystem(files);

  const machine::vfs_result<machine::dos_path> where = machine::canonicalize(
      machine::dos_path{},
      std::span<const char>(opts.program.data(), opts.program.size()));
  if (!where.ok()) {
    std::fprintf(stderr, "amberfolio: %s is not a usable DOS name\n",
                 opts.program.c_str());
    return EXIT_FAILURE;
  }

  // The identity of the player's file, printed before anything runs.
  //
  // A fact about the file, not content from it (CONTRIBUTING.md), and the
  // one M4's fingerprint table will key its seams on (PLAN.md §2, §5). It
  // is printed even when the load then fails, because "which file was
  // this" is the first question anybody asks of a boot log and a load
  // that failed is exactly when it matters.
  const machine::vfs_result<sha256_digest> identity =
      machine::fingerprint_file(files, where.value);
  if (identity.ok()) {
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(identity.value, hex));
    std::fprintf(stderr, "amberfolio: load %s sha256=%s\n",
                 opts.program.c_str(), hex.data());
  } else {
    std::fprintf(stderr,
                 "amberfolio: load %s could not be fingerprinted"
                 " (vfs error %u)\n",
                 opts.program.c_str(), static_cast<unsigned>(identity.error));
  }

  // Asked for before the program is loaded, so that the ring covers the
  // whole run rather than starting a few instructions into it. It is a
  // setting on the machine and survives every reset (trace.h).
  box.trace().enable(opts.trace);
  log.set_tracing(opts.trace);

  const machine::loader_result<machine::loaded_program> loaded =
      machine::load_program(box, files, where.value,
                            std::span<const char>(opts.command_tail.data(),
                                                  opts.command_tail.size()));
  if (!loaded.ok()) {
    std::fprintf(stderr, "amberfolio: cannot load %s (loader error %u)\n",
                 opts.program.c_str(), static_cast<unsigned>(loaded.error));
    return EXIT_FAILURE;
  }
  std::fprintf(stderr,
               "amberfolio: load psp=%04X image=%04X entry=%04X:%04X"
               " stack=%04X:%04X tail=%zu\n",
               loaded.value.psp_segment, loaded.value.load_segment,
               loaded.value.entry_cs, loaded.value.entry_ip,
               loaded.value.entry_ss, loaded.value.entry_sp,
               opts.command_tail.size());

  // The seams the run was asked for, now that there is a program to key
  // them on. Enabled after the load and before the first step, and each
  // one printed: a run with a seam on is not the same run as one without
  // it, and a log that did not say so would be describing the wrong
  // machine (machine/seam.h).
  //
  // First the identity: which known edition the fingerprint names, or
  // that it names none — in which case the game runs as a plain machine
  // and every seam is unavailable (machine/edition.h, PLAN.md §5). Said
  // either way, because "no seams for this file" is a finding and not a
  // silence.
  if (identity.ok()) {
    box.seams().loaded(identity.value, loaded.value.load_segment);
    if (const machine::edition* known = box.seams().known_edition();
        known != nullptr) {
      std::fprintf(stderr, "amberfolio: edition %.*s\n",
                   static_cast<int>(known->name.size()), known->name.data());
    } else {
      std::fprintf(stderr,
                   "amberfolio: edition unrecognized - no seams are"
                   " available for this program\n");
    }
  }
  for (const std::string& id : opts.seams) {
    const machine::seam_error why = box.seams().enable(id);
    if (why == machine::seam_error::none) {
      continue;
    }
    std::fprintf(stderr, "amberfolio: seam %s refused (%s)\n", id.c_str(),
                 seam_refusal(why));
    return EXIT_FAILURE;
  }

  if (opts.list_seams) {
    // The listing #98 asks for, in the state the run would have started
    // in, and then nothing runs: a listing is a question, and the answer
    // is the whole of what was asked for.
    const machine::seam_engine& seams = box.seams();
    for (std::size_t i = 0; i < seams.count(); ++i) {
      const machine::seam_status row = seams.status(i);
      std::fprintf(stderr, "amberfolio: seams %.*s %s%s%s%s - %.*s\n",
                   static_cast<int>(row.id.size()), row.id.data(),
                   machine::seam_state_name(row.state),
                   row.state == machine::seam_state::on
                       ? (row.armed ? " armed" : " inert")
                       : "",
                   row.reason == machine::seam_reason::none ? "" : " ",
                   row.reason == machine::seam_reason::none
                       ? ""
                       : machine::seam_reason_name(row.reason),
                   static_cast<int>(row.about.size()), row.about.data());
    }
    return EXIT_SUCCESS;
  }

  // --- Replay: load the recording and become the run it describes -------
  //
  // The recording decides the speed, the seams and every key (machine/
  // replay.h); this host's job is to be that machine and check each
  // checkpoint. Set up before SDL, so a mismatch of the initial
  // conditions is reported without a window ever opening.
  std::string replay_text;
  machine::replay_player player;
  const bool replaying = !opts.replay_path.empty();
  if (replaying) {
    std::ifstream in(opts.replay_path, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "amberfolio: cannot read %s\n",
                   opts.replay_path.c_str());
      return EXIT_FAILURE;
    }
    replay_text.assign(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    if (!player.load(
            std::span<const char>(replay_text.data(), replay_text.size()))) {
      std::array<char, machine::replay_report_capacity> line{};
      static_cast<void>(player.report(line));
      std::fputs(line.data(), stderr);
      return EXIT_FAILURE;
    }
    // The recording's own speed and seams, applied before it is checked
    // against the machine: a replay is the run the recording names.
    box.set_step_cost_subticks(player.preamble().subticks);
    for (std::size_t i = 0; i < player.preamble().seam_count; ++i) {
      if (box.seams().enable(player.preamble().seam(i)) !=
          machine::seam_error::none) {
        std::fprintf(stderr,
                     "amberfolio: the recording's seam %.*s is not"
                     " available for this program\n",
                     static_cast<int>(player.preamble().seam(i).size()),
                     player.preamble().seam(i).data());
        return EXIT_FAILURE;
      }
    }
    if (player.check_initial(box, &files) != machine::replay_status::ok) {
      std::array<char, machine::replay_report_capacity> line{};
      static_cast<void>(player.report(line));
      std::fputs(line.data(), stderr);
      return EXIT_FAILURE;
    }
  }

  // --- Record: the preamble now, the stream as the run goes -------------
  std::ofstream recording;
  if (!opts.record_path.empty()) {
    recording.open(opts.record_path, std::ios::binary | std::ios::trunc);
    if (!recording) {
      std::fprintf(stderr, "amberfolio: cannot write %s\n",
                   opts.record_path.c_str());
      return EXIT_FAILURE;
    }
    std::array<char, 16384> preamble{};
    const std::size_t n =
        machine::write_preamble(box, files, opts.program,
                                std::span<const char>(opts.command_tail.data(),
                                                      opts.command_tail.size()),
                                preamble);
    if (n == 0) {
      std::fprintf(stderr,
                   "amberfolio: could not record this run's initial"
                   " conditions\n");
      return EXIT_FAILURE;
    }
    recording.write(preamble.data(), static_cast<std::streamsize>(n));
  }

  // One line of a recording, written to `recording` if it is open. Every
  // line of the stream goes through here, so that a run without --record
  // pays one branch, and so that this host's spelling of a line and the
  // player's stay the one spelling in replay.h.
  const auto record_line = [&recording](const machine::replay_event& event) {
    if (!recording.is_open()) {
      return;
    }
    std::array<char, machine::replay_max_line> line{};
    const std::size_t n = machine::format_replay_line(event, line);
    recording.write(line.data(), static_cast<std::streamsize>(n));
  };

  const std::uint32_t init_flags =
      opts.headless ? 0U : (SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (!SDL_Init(init_flags)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  // The parsed presses, with room to record which have gone. `opts` is
  // what the command line said and stays that way.
  std::vector<scripted_press> presses = opts.presses;

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  audio_bridge bridge;
  bridge.box = &box;

  // `--dump`'s WAV, sized once and never resized: the audio thread
  // appends to it and must not allocate. A minute of virtual time is
  // enough to hear a title sequence through and small enough to be free
  // on any machine that can run this at all; a run past it keeps going
  // and the file simply ends where the buffer did, which is said out
  // loud rather than left to be noticed.
  if (!opts.dump_prefix.empty()) {
    bridge.capture.assign(std::size_t{audio_sample_rate} * dump_audio_seconds,
                          0.0F);
  }

  if (!opts.headless) {
    const int w = static_cast<int>(machine::frame_width * opts.scale);
    const int h = static_cast<int>(machine::frame_height * opts.scale);
    if (!SDL_CreateWindowAndRenderer("amberfolio", w, h, 0, &window,
                                     &renderer)) {
      std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n",
                   SDL_GetError());
      SDL_Quit();
      return EXIT_FAILURE;
    }
    // Now, and not at parse time: what SDL calls a key is a question
    // about SDL's own tables, and asking it before SDL_Init is asking it
    // early.
    for (scripted_press& press : presses) {
      press.code = SDL_GetScancodeFromName(press.key.c_str());
      if (press.code == SDL_SCANCODE_UNKNOWN) {
        std::fprintf(stderr, "amberfolio: SDL has no key called '%s'\n",
                     press.key.c_str());
        SDL_Quit();
        return EXIT_FAILURE;
      }
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                static_cast<int>(machine::frame_width),
                                static_cast<int>(machine::frame_height));
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bridge.box = &box;
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                             .channels = 1,
                             .freq = static_cast<int>(audio_sample_rate)};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      feed_audio, &bridge);
    if (audio != nullptr) {
      SDL_ResumeAudioStreamDevice(audio);
    }
  }

  // One virtual frame is the renderer's own period, so the loop and the
  // renderer agree by construction rather than by two constants matching.
  const machine::ticks frame_ticks = machine::renderer::frame_period;
  std::uint64_t presented = 0;
  std::uint64_t frame_index = 0;
  verify_report report;
  std::vector<std::uint32_t> argb(machine::frame_pixels);
  bool quit = false;

  // How much of the speaker's timeline one frame of virtual time is
  // worth, in samples. Only pulled when there is no audio device doing
  // the pulling — see the call site.
  const auto frame_samples = static_cast<std::size_t>(
      (static_cast<std::uint64_t>(audio_sample_rate) * frame_ticks) /
      machine::pit_input_hz);
  std::vector<float> headless_audio(frame_samples);

  machine::run_end ended = machine::run_end::stopped;
  std::uint64_t overlays_printed = 0;

  // The player is primed before the first slice: `next_tick()` answers
  // only once `apply()` has looked at the recording, and an event
  // recorded at tick 0 — a key on the very first frame — has to be
  // delivered before the machine has taken a step, not after.
  if (replaying) {
    static_cast<void>(player.apply(box));
  }

  for (;;) {
    if (box.stopped()) {
      ended = machine::run_end::stopped;
      break;
    }
    // A replay that reached the recording's `end` has verified all of it,
    // and one that diverged has nothing left worth running: every tick
    // after the first difference is about a machine the recording never
    // described. Either way this is where the loop ends, and the report
    // below says which it was.
    //
    // `host_quit` because that is what this is from the machine's side:
    // it was still running and something outside it said stop. The
    // machine's own ending is checked first, above, so a replay of a
    // program that exits still reports the exit.
    if (replaying && player.status() != machine::replay_status::ok) {
      ended = machine::run_end::host_quit;
      break;
    }
    if (quit) {
      ended = machine::run_end::host_quit;
      break;
    }
    if (opts.step_budget != 0 && box.steps() >= opts.step_budget) {
      ended = machine::run_end::step_budget;
      break;
    }
    if (opts.tick_budget != 0 && box.time() >= opts.tick_budget) {
      ended = machine::run_end::tick_budget;
      break;
    }

    const auto frame_started = std::chrono::steady_clock::now();

    // Before the frame is run rather than after: `frame_index` is the
    // number `--press KEY@FRAME` matches on, and a still named for a
    // frame should be the screen that frame's keystroke was answered
    // against, not the one after it.
    if (opts.dump_every != 0 && frame_index % opts.dump_every == 0) {
      write_still(opts.dump_prefix, frame_index, box);
    }

    if (!opts.headless) {
      // Pushed before the poll, so the events this frame owes are on the
      // queue by the time the queue is read - one loop iteration, not
      // two.
      for (scripted_press& press : presses) {
        if (!press.done && press.frame == frame_index) {
          push_key_event(window, press.code, true);
          push_key_event(window, press.code, false);
          press.done = true;
        }
      }

      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
          quit = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN ||
                   event.type == SDL_EVENT_KEY_UP) {
          const std::uint8_t code = sdl::xt_scancode(event.key.scancode);
          // A replay's keys are the recording's, delivered by the player
          // at the ticks it names. A key struck at the window during one
          // would be an input the recorded run never had, so the window
          // still closes and nothing else gets through.
          if (code != 0 && !event.key.repeat && !replaying) {
            const machine::key_action action = event.type == SDL_EVENT_KEY_DOWN
                                                   ? machine::key_action::down
                                                   : machine::key_action::up;
            box.post_key(code, action);
            ++report.keys;
            // Recorded where it is posted and at the tick it is posted
            // at: the machine's clock is the only stamp a key has, and
            // the post is the only moment the machine can see one.
            machine::replay_event line{};
            line.kind = machine::replay_line::key;
            line.at = box.time();
            line.scancode = code;
            line.action = action;
            record_line(line);
          }
        }
      }
    }

    // Virtual time first, and to a boundary the machine chose. Nothing
    // about how long the last frame took on the wall gets to influence
    // how much machine time passes here. A budget may bring the boundary
    // closer; nothing may push it further out.
    box.run(slice_end(box, frame_ticks, opts.step_budget, opts.tick_budget,
                      replaying ? player.next_tick() : machine::never));
    drain_console(box);
    if (opts.trace) {
      print_overlay_loads(box, overlays_printed);
    }

    // Then the events this slice ran up to, delivered and checked before
    // anything else looks at the machine: a checkpoint is a statement
    // about the machine at a tick, and the display and the speaker are
    // read from it just below.
    if (replaying) {
      static_cast<void>(player.apply(box));
    }

    // A checkpoint a frame. The boundary is the machine's own — frame
    // ticks off its clock, never the wall — so a recording made on one
    // target names ticks a replay reaches on every other. Taken after the
    // slice and before the frame is presented, which is the moment the
    // run has just finished being somewhere describable.
    if (recording.is_open()) {
      record_line(machine::checkpoint_of(box));
    }

    // With no audio device there is nobody pulling the speaker, so a
    // `--dump` run has to pull it here, on the machine thread — which the
    // threading contract permits ("exactly one thread", and this is it).
    // Not done when a device is open: two consumers of one timeline would
    // each get half the samples (platform.h).
    if (audio == nullptr && !bridge.capture.empty()) {
      static_cast<void>(box.audio().render(headless_audio, audio_sample_rate));
      capture_samples(bridge, headless_audio);
    }

    if (!opts.headless && box.display().generation() != presented) {
      presented = box.display().generation();
      const std::span<const std::uint8_t> pixels = box.display().pixels();
      const std::span<const machine::rgb> palette = box.display().palette();
      for (std::size_t i = 0; i < argb.size(); ++i) {
        const machine::rgb color = palette[pixels[i] & 0x0FU];
        argb[i] = (static_cast<std::uint32_t>(color.red) << 16) |
                  (static_cast<std::uint32_t>(color.green) << 8) |
                  static_cast<std::uint32_t>(color.blue);
      }
      SDL_UpdateTexture(
          texture, nullptr, argb.data(),
          static_cast<int>(machine::frame_width * sizeof(std::uint32_t)));
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
      if (opts.verify) {
        verify_target(renderer, argb, report);
      }
      SDL_RenderPresent(renderer);
      ++report.presented;
    }

    if (!opts.headless && opts.fast != 0.0) {
      // Whatever wall time is left of this frame, and never a negative
      // one: a host that fell behind simply does not sleep. It does not
      // then run the machine faster to compensate — see this file's top
      // comment.
      //
      // `--fast N` divides the budget and nothing else. The machine has
      // already been run to the same tick it would have been run to
      // anyway; all that changes is how long this thread waits before
      // going round again, which is the one place wall time is allowed
      // to appear at all.
      const auto spent = std::chrono::steady_clock::now() - frame_started;
      const auto budget = std::chrono::duration<double>(
          static_cast<double>(frame_ticks) / machine::pit_input_hz / opts.fast);
      const auto left =
          std::chrono::duration_cast<std::chrono::milliseconds>(budget - spent);
      if (left.count() > 0) {
        std::this_thread::sleep_for(left);
      }
    }

    ++frame_index;
  }

  // Where the recording stops, written before SDL comes down so that a
  // run whose teardown goes wrong still leaves a recording saying how far
  // it got. A player that reaches this line verified everything before
  // it; one that runs out of text without it says so.
  if (recording.is_open()) {
    machine::replay_event last{};
    last.kind = machine::replay_line::end;
    last.at = box.time();
    last.steps = box.steps();
    record_line(last);
    recording.flush();
  }

  // The audio stream first, and before the counters below are read: it is
  // what stops the callback thread, and until it has returned the two
  // tallies are still being written to.
  if (audio != nullptr) {
    SDL_DestroyAudioStream(audio);
  }
  if (texture != nullptr) {
    SDL_DestroyTexture(texture);
  }
  if (renderer != nullptr) {
    SDL_DestroyRenderer(renderer);
  }
  if (window != nullptr) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();

  // The stop report: the whole point of this host in M3, formatted in
  // core so that the browser prints the same sentence (machine/report.h).
  // After SDL is down, so that nothing SDL writes on its way out can land
  // in the middle of it.
  {
    std::array<char, machine::stop_report_capacity> text{};
    machine::format_stop_report(box, ended, text);
    std::fputs(text.data(), stderr);
  }
  if (opts.trace) {
    std::vector<char> text(machine::trace_report_capacity);
    machine::format_trace_report(box, text);
    std::fputs(text.data(), stderr);
  }

  if (!opts.dump_prefix.empty()) {
    const std::filesystem::path ppm(opts.dump_prefix + ".ppm");
    if (sdl::write_ppm(ppm, box.display().pixels(), box.display().palette())) {
      std::fprintf(stderr, "amberfolio: dump frame=%s generation=%llu\n",
                   ppm.string().c_str(),
                   static_cast<unsigned long long>(box.display().generation()));
    } else {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   ppm.string().c_str());
    }

    // Whatever the one consumer managed to put there, whichever thread it
    // was; the stream is destroyed by now, so the callback cannot still
    // be writing.
    const std::size_t captured =
        bridge.captured.load(std::memory_order_relaxed);
    const std::filesystem::path wav(opts.dump_prefix + ".wav");
    if (captured == 0) {
      // Told apart from a failed write on purpose: "the speaker made no
      // sound this run" and "this file could not be created" are two
      // different findings, and only one of them is about the machine.
      std::fprintf(stderr,
                   "amberfolio: dump no audio was captured (nothing pulled"
                   " the speaker)\n");
    } else if (sdl::write_wav(
                   wav, std::span<const float>(bridge.capture.data(), captured),
                   audio_sample_rate)) {
      std::fprintf(stderr, "amberfolio: dump audio=%s samples=%zu%s\n",
                   wav.string().c_str(), captured,
                   bridge.truncated.load(std::memory_order_relaxed)
                       ? " (truncated)"
                       : "");
    } else {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   wav.string().c_str());
    }
  }

  // What the recording said, and whether this machine was it. Printed
  // before `--verify`'s tally and answered before the program's own exit
  // code, for the reason `--verify` is: a run asked to check itself
  // against a recording is answering the check's question, not the
  // program's. A replay that did not reach the recording's `end` failed,
  // whatever else it did — a run cut short verified a prefix, and a
  // prefix is not the run.
  if (replaying) {
    std::array<char, machine::replay_report_capacity> line{};
    static_cast<void>(player.report(line));
    std::fputs(line.data(), stderr);
    if (!player.done()) {
      std::fflush(stdout);
      return EXIT_FAILURE;
    }
  }

  // And where a recording went, so that the file's name and the tick it
  // stops at are in the same log as the run that made it.
  if (recording.is_open()) {
    std::fprintf(stderr, "amberfolio: record %s tick=%llu steps=%llu\n",
                 opts.record_path.c_str(),
                 static_cast<unsigned long long>(box.time()),
                 static_cast<unsigned long long>(box.steps()));
  }

  if (opts.verify) {
    report.composed = box.display().generation();
    std::fprintf(stderr,
                 "amberfolio: verify - composed %llu, presented %llu,"
                 " checked %llu, mismatched pixels %llu\n",
                 static_cast<unsigned long long>(report.composed),
                 static_cast<unsigned long long>(report.presented),
                 static_cast<unsigned long long>(report.checked),
                 static_cast<unsigned long long>(report.mismatched));
    std::fprintf(stderr,
                 "amberfolio: verify - audio callbacks %llu, audio samples"
                 " %llu, sounded %llu, keys posted %llu\n",
                 static_cast<unsigned long long>(
                     bridge.callbacks.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(
                     bridge.samples.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(
                     bridge.sounded.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(report.keys));
    if (report.unreadable != 0 || report.odd_size != 0) {
      std::fprintf(stderr,
                   "amberfolio: verify - %llu targets would not read back,"
                   " %llu were not a whole multiple of the frame\n",
                   static_cast<unsigned long long>(report.unreadable),
                   static_cast<unsigned long long>(report.odd_size));
    }

    // What makes this a check rather than a printout. A run that
    // presented nothing proves nothing, and neither does one whose every
    // present was unreadable - so both are failures, in the same breath
    // as a picture that came back wrong.
    const char* wrong = nullptr;
    if (report.presented == 0) {
      wrong = "nothing was ever presented";
    } else if (report.checked == 0) {
      wrong = "no presented frame could be read back and compared";
    } else if (report.mismatched != 0) {
      wrong = "the presented picture is not the one that was uploaded";
    }
    if (wrong != nullptr) {
      std::fprintf(stderr, "amberfolio: verify FAILED - %s\n", wrong);
      std::fflush(stdout);
      return EXIT_FAILURE;
    }
    std::fprintf(stderr, "amberfolio: verify OK\n");
  }

  const machine::stop_record& stop = box.stop();
  if (ended == machine::run_end::stopped &&
      stop.reason == machine::stop_reason::program_exited) {
    std::fflush(stdout);
    return static_cast<int>(stop.exit_code);
  }
  if (ended == machine::run_end::host_quit) {
    return EXIT_SUCCESS;
  }

  // Everything else is a run that did not finish: a machine that refused
  // something, or a budget that ran out with the program still going.
  // The report above has already said which and where; this is only the
  // process's answer, and it is failure either way because in neither
  // case did the program get to choose one.
  std::fflush(stdout);
  return EXIT_FAILURE;
} catch (const std::exception& e) {
  // A function-try-block on main, because everything above allocates -
  // the megabyte of machine, the frame buffer, the host's own strings -
  // and a host that lets an allocation failure escape as an unhandled
  // exception tells the player nothing at all.
  std::fprintf(stderr, "amberfolio: %s\n", e.what());
  return EXIT_FAILURE;
} catch (...) {
  std::fprintf(stderr, "amberfolio: unknown error\n");
  return EXIT_FAILURE;
}
