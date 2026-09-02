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
//                                     [--watch OFF[:N]]
//                                     [--seam ID] [--seams]
//                                     [--document PATH] [--vfs-list]
//                                     [--vfs-get PATH] [--vfs-remove PATH]
//                                     [--speed NAME]
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
//   --dump PREFIX    write PREFIX.ppm, PREFIX.wav and PREFIX.edges
//
//     The frame the machine composed and the sound it made, in two
//     formats every viewer opens (dump.h). docs/machine.md §7's warning
//     about goldens is the argument: "the title renders" is a claim to
//     look at, and no test in this repository will ever run the file
//     that produces it.
//
//     `PREFIX.edges` is the third, and it is a different kind of thing
//     from the other two (M4-A1, #106). The WAV is *a rendering* of the
//     sound at 48 kHz through a box filter; the edges file is the sound
//     as the machine holds it — one line per output transition, `tick
//     level`, in PIT input ticks. platform.h calls the edge list the
//     canonical audio state and says the floats are not it, and #106
//     wants the two questions kept apart: whether the machine made the
//     right edges at the right ticks, and whether the render of them is
//     right. Only the second used to be inspectable at all.
//
//     Written as the run goes, so it survives a run that ends badly, and
//     it ends with a `# edges N dropped M` line saying whether it is all
//     of them.
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
//   --watch OFF[:N]  print a data word every time it changes
//
//     The third instrument of the same kind, and it arrived for the same
//     reason the other two did: a leg of docs/playable.md was impractical
//     without it. `--dump-every` says what the screen did and `--trace`
//     says what the program asked DOS for; neither says *where the party
//     is*, and a city service three streets away is a navigation problem
//     before it is an emulation one (#104).
//
//     `OFF` is a hexadecimal offset in the program's data segment — the
//     anchor its globals are counted in, and the one a seam reads them
//     through (seam_cheats.cpp) — and `N` is 1 or 2 bytes, default 1.
//     Repeatable. A line is printed only when one of the watched values
//     differs from the last line's, so a run that walks twenty squares
//     prints twenty lines:
//
//         amberfolio: watch frame=011300 ds=0CDC 6AAD=05 6AAE=0C 6AAF=06
//
//     The segment is on every line because it is not always the data
//     segment: DS is whatever the frame boundary landed on, and a frame
//     that ends inside an interrupt or an overlay has it pointed
//     somewhere else. What a watch has already said is remembered per
//     segment, so each of those says its piece once and then stays quiet,
//     and the lines that are left are the ones where the watched values
//     actually moved. Filter on the segment the globals live in and the
//     log is the movement and nothing else.
//
//     It reads `memory_map::ram()` and not the bus, so it takes no bus
//     cycle, disturbs no EGA latch and cannot make a notice of its own: a
//     watch has to be able to say a run was clean, which it could not do
//     if watching were itself something the run did.
//
//     What it is *not* is a debugger, and deliberately: no writes, no
//     breakpoints, no expressions. Machine state is the seam engine's to
//     touch (PLAN.md §5) and nothing else's, and this reads.
//
//   --trace          keep the trace ring, and print it with the report
//
//     Off by default, in the machine, at a cost of one branch per step
//     (machine/trace.h). What it answers is the question a bare address
//     cannot: how the program got there.
//
//     The ring's third channel is the naming file calls (#121) — the last
//     thirty-two opens, creates, mkdirs, unlinks and closes, each with
//     the path it resolved to and what DOS answered:
//
//         amberfolio: stop trace file=open \POR\POOL.CFG handle=0000 path_not_found from=0B58:1458
//
//     A failed open is a legitimate DOS answer and stays one, so nothing
//     stops and nothing is refused; the line is the whole of the fix.
//     Without it "the program has spent its startup asking for a file
//     that is not there" is a directory audit rather than a report.
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
//   --automap-store  keep the automap's exploration beside the save
//
//     M5-E2c (#173). What the automap seam has explored is observation
//     and not machine state, so it is gone when the machine stops. This
//     writes it into `\SAVE\AFMAP.DAT` — a file of this project's own,
//     beside the program's saves and never inside one — and reads it
//     back at startup, with a snapshot per save slot so two playthroughs
//     do not share one map.
//
//     Off by default, and deliberately: this is a real directory of the
//     player's, and a file appearing in it changes it. Every recorded
//     session in `tests/sessions` pins its disk by name, size and
//     SHA-256, so a sidecar written by a verification run would make the
//     next run's disk a different disk.
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
//     `code-wheel` is **gated on the code wheel** (#115): turning it on
//     without `--document` leaves it inert and saying so, because the
//     possession gate PLAN.md §5 requires is applied where residency is.
//
//   --document PATH  present a document the player holds
//
//     PLAN.md §5 gates two enhancements on a fingerprint-verified
//     document — the code-wheel bypass on the code wheel, the journal on
//     the journal — and the rule is exact: a possession gate, which
//     demonstrates the player holds the document and no more. This is
//     the presenting side (#171): the file is read, hashed, and dropped.
//     Nothing is parsed and nothing is kept.
//
//     `PATH` is a path on *this* machine, not on the emulated one: a
//     code wheel lives wherever a person keeps their PDFs, which is very
//     often not inside the game directory. Repeatable, because a player
//     may hold both documents.
//
//     A document this build does not know is **reported, not guessed**:
//     the line says so and prints the fingerprint of the file, which is
//     what turns "this does not work" into something somebody can add to
//     `machine/document.h`'s table. A gate that armed on an unrecognized
//     document would be a gate that armed on anything.
//
//     Configuration, like `--seam`: it is applied before the first step,
//     it is not machine state, and a run with a document presented and
//     every seam off is byte-for-byte the run without one.
//
//   --journal PATH   ingest the player's own Adventurer's Journal
//
//     M5-E3 (#174), and the one place this host reads *inside* a
//     document rather than only hashing it. The file is presented as
//     `--document` presents one — so a journal-gated seam arms — and then
//     each entry's scan is followed to its offset, decoded, read by an
//     OCR engine and kept as text (`host/journal_ingest.h`).
//
//     An edition this build does not know the insides of is reported
//     with its fingerprint and nothing is read: the offsets are only
//     true of one file, and following them into another produces
//     twenty failures rather than one sentence. `known_journals()` has
//     one row — the archive release's own journal (#214) — so that is
//     what every *other* journal gets, and `docs/journal.md` §3 is how
//     an edition is added.
//
//   --journal-store PATH  where the text a journal ingested lives
//
//     Defaults to `journal.txt` under this platform's per-user data
//     directory, beside where M6's configuration will live; the line
//     this host prints after an ingestion says which file it used. The
//     store is read before an ingestion and written after, which is what
//     makes a correction survive one (`host/journal_store.h`).
//
//     It is read at the start of **every** run, `--journal` or not: an
//     ingestion happens once and the reading happens for ever after
//     (M5-E4, #175), so a player who ingested their journal last week
//     starts today's run with `--seam journal` able to answer.
//
//   --journal-ocr PATH|none  which OCR engine to read with
//
//     The player's own Tesseract, `tesseract` off the path by default
//     (`tesseract_ocr.h` says why it is run rather than linked).
//     `none` ingests every image and stores no text, which is also what
//     happens when the engine is not installed — said in as many words
//     rather than quietly recognizing nothing.
//
//   --journal-probe  add the synthetic probe edition, for checks
//
//     Test apparatus, and named as such: `host/journal_probe.h` builds a
//     document this project generates, so the whole ingestion can be
//     driven in CI without a document it did not make. It adds the probe
//     edition to *this run's* table and installs the fixture engine that
//     answers for exactly the probe's pixels; a player's build knows
//     nothing about either, the same way it knows nothing about the web
//     host's probe seam.
//
//   The journal reader itself is a seam, not a flag: `--seam journal`
//   turns it on, and then the entry the game cites opens on the game's
//   own screen and F1 opens any other (M5-E4, #175, `machine/journal.h`).
//   It reads the store above and never writes it.
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
//   --vfs-list       list every file on the disk, after the run
//   --vfs-get PATH   read one file back through the door, after the run
//   --vfs-remove PATH  delete one file, after the run
//
//     M5-D2's door (#170), driven against a real directory. The wasm
//     host reaches these operations through `af_machine_vfs_*`, over an
//     in-memory filesystem a browser handed it a file at a time; this is
//     the same three operations over `directory_vfs`, so the pair can be
//     compared and #173's exploration sidecar can be checked on either
//     host. The listing walks the whole tree and is **files** - a path
//     and a size per line, in the pinned walk order core decides
//     (machine/vfs.h), which is why an empty directory does not appear.
//
//     They run **after** the program has exited, because the question
//     they exist to answer is what the run left behind: what is in
//     `\\SAVE\\` once the game has saved.
//
//     `--vfs-get` prints the file's size and the SHA-256 of the bytes
//     that came back, and not the bytes. Every byte goes through the
//     read, which is what is being checked; putting a player's file into
//     a log would be putting it somewhere it does not belong, and a
//     digest of what was read is a stronger claim than a hexdump anybody
//     would actually check by eye.
//
//     `--vfs-remove` deletes a real file on the player's disk. It says
//     so on the line before it does it, because this host's filesystem
//     is not a sandbox and a flag that reads like a test fixture is
//     exactly the one somebody runs on a real installation.
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
//   --record-every N take a checkpoint every N frames, not every one
//   --replay FILE    be the run a recording describes, and check it
//
//     The two halves of machine/replay.h, and the reason that file says
//     the player never runs the machine: this loop does. Recording adds
//     three things to it — a key line where a key is posted, a checkpoint
//     where a frame ends, an `end` line where the run does — and the
//     preamble, written before SDL is even up.
//
//     `--record-every` spaces the second of those. A checkpoint hashes
//     every byte of RAM, so one a frame is about a megabyte of SHA-256
//     sixty times a virtual second: right for a run somebody is pointing
//     at a problem, and the reason a game-length recording at that
//     cadence is both slow to make and far too large to commit. The
//     session library picks its own and says why
//     (tests/sessions/README.md).
//
//     What a sparse cadence costs is *where* a divergence is localized,
//     never whether one is found: every key still lands on the tick it
//     was recorded at, and the run still has to reach `end`. What it must
//     not cost is the moments worth pinning, so a frame that posted a key
//     is checkpointed whatever N says, and so is the frame the run ends
//     on — that last one carries `stopped`, which a replaying host needs
//     before it can arrive at the tick at all.
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
// Volume and mute are this host's too, and only this host's (M4-A1
// remainder, #148). `--volume PERCENT` and `--mute` set where the level
// starts; F11 toggles the mute and F12 steps the volume while the run is
// going. `audio_gain.h` argues at length why none of it is in core; the
// short version is that a gain inside `render()` would stop the samples
// being the exact integral of the edge list, which is the same objection
// platform.h already makes to a high-pass there.
//
// Two consequences of that placement are worth stating where somebody
// reading a run's output will meet them:
//
//   * **`--dump`'s WAV is written before the gain**, so it is what the
//     machine made rather than what this host chose to play. A muted run
//     still dumps its tone, which is the answer docs/hosts.md §3 wants
//     when the question is "is the fault in the machine or in the host".
//     The `.edges` file was never anywhere near it.
//   * **`--verify`'s `sounded` count is taken after the gain**, because
//     that number's whole job is to say what reached SDL's stream. A
//     muted run therefore reports `sounded 0` truthfully, and
//     `sdl-host-mutes-the-tone` is exactly that claim.
//
// F11 and F12 are host keys and cost the emulated program nothing: an
// 83-key XT keyboard has ten function keys, so `sdl::xt_scancode()`
// answers 0 for both and there is no scan code for them to have been
// taken from. `keymap_test.cpp` pins that, because it is the assumption
// the binding rests on.
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
//                      own thread, and what the timeline thought of the
//                      pacing while it did it — underruns, resyncs and
//                      dropped edges (M4-A1, #106). Report all of it on
//                      stderr at exit, and fail the process if the
//                      picture did not match or if nothing was ever
//                      presented.
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "amberfolio/cpu/registers.h"
#include "amberfolio/host/automap_store.h"
#include "amberfolio/host/host_services.h"
#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/document.h"
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
#include "audio_gain.h"
#include "directory_vfs.h"
#include "dump.h"
#include "keymap.h"
#include "tesseract_ocr.h"
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
#include "tesseract_linked_ocr.h"
#endif

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

/// The rungs F12 steps the volume between (#148). Four of them, and a
/// wrap from the top back to the bottom: one key has to cover a whole
/// axis — F11 and F12 are the only two keys an XT keyboard has no scan
/// code for, so they are the only two this host may take without
/// stealing one from the program — and if a wrap has to surprise
/// somebody it should surprise them quietly rather than loudly.
constexpr std::array<float, 4> volume_rungs{0.25F, 0.50F, 0.75F, 1.00F};

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

/// Reports what the core would not fake, to stderr. A host has to have
/// one of these or "log, don't fake" is only half a mechanism
/// (machine/diagnostics.h).
///
/// **The sentences are not this host's.** Every line below is rendered by
/// `machine::format_diagnostic` (machine/report.h), for the reason that
/// file gives at length: the browser has a sink of its own now
/// (machine/log.h, M4-W1 #108), and two hosts writing their own version
/// of the same line would produce two accounts that look alike and can
/// quietly differ. This host's job is to put the characters on stderr.
class stderr_diagnostics final : public machine::diagnostics {
 public:
  /// The exploration sidecar, if this run has one (M5-E2c, #173).
  ///
  /// It is fed from here because this is where the DOS layer's file
  /// events arrive, and the store learns which save slot the program
  /// touched from nothing else — the program keeps no slot letter in
  /// memory to read (`hosts/common/.../automap_store.h`). Null on every
  /// run that did not ask for it, which is every run in `tests/sessions`.
  void set_automap_store(host::automap_store* store) noexcept {
    automap_ = store;
  }

  /// Whether to print every service call and file event as it happens.
  /// Off by default, for the reason diagnostics.h gives: a call is
  /// something the program *did*, not a symptom of anything, and a boot
  /// makes tens of thousands of them. `--trace` turns it on, so that the
  /// live stream and the ring dumped at the end are one facility asked
  /// for once.
  void set_tracing(bool on) noexcept { tracing_ = on; }

  void report(const machine::notice& what) override { write(what); }

  void report(const machine::stop_record& stop) override {
    // A program exiting produces no line at all — report.h owns that
    // rule, so that this host and the browser agree about it.
    write(stop);
  }

  void report(const cpu::stop_record& stop) override { write(stop); }

  void report(const machine::device_stop& stop) override { write(stop); }

  void report(const machine::seam_event& event) override { write(event); }

  void report(const machine::file_event& event) override {
    if (automap_ != nullptr) {
      automap_->saw(event);
    }
    if (!tracing_) {
      return;
    }
    write(event);
  }

  void report(const machine::service_call& call) override {
    if (!tracing_) {
      return;
    }
    write(call);
  }

 private:
  /// Render one record and put it on stderr. Nothing is printed for a
  /// record that has no line.
  template <typename T>
  void write(const T& record) noexcept {
    std::array<char, machine::diagnostic_line_capacity> line{};
    if (machine::format_diagnostic(record, line) == 0) {
      return;
    }
    std::fputs(line.data(), stderr);
  }

  host::automap_store* automap_{nullptr};

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
///
/// `gain` is the volume control (#148), and it is the only thing in this
/// struct the main thread *writes* while the callback may be running. Its
/// own header says why it is a `std::atomic<float>` and not a lock: the
/// callback may not wait, and a level is a value rather than a handshake.
struct audio_bridge {
  machine::machine* box{};
  std::vector<float> scratch;
  sdl::audio_gain gain{audio_sample_rate};
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

  // Captured before the gain and played after it. `--dump`'s WAV is a
  // rendering of what the *machine* made — the artefact docs/hosts.md §3
  // sends a person to when they are trying to tell a machine fault from a
  // host fault — and a listening level is no part of that. What goes to
  // the device is the other thing, and `sounded` below counts that one.
  capture_samples(*bridge, out);

  bridge->gain.apply(out);
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
    case machine::seam_error::document_not_presented:
      // The one refusal a *person* can do something about (#171), so it
      // says what to do rather than only what is wrong.
      return "this seam needs a document you have not presented - show it"
             " with --document";
    case machine::seam_error::call_did_not_return:
      // Also never an answer to `enable()`: the engine produces it when a
      // seam called into the program and the call did not come back
      // (#188), and it means a fact table naming an address that is not
      // the routine it says it is.
      return "this seam called into the program and the call did not come"
             " back";
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
    case machine::seam_error::not_triggered:
      return "this seam is not one you pull; it acts whenever it is on";
    case machine::seam_error::not_enabled:
      return "this seam is off - turn it on with --seam before pulling it";
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

/// A seam trigger the host pulls for itself, at a frame of the loop
/// (#161) — `scripted_press`'s sibling, and the reason it is a separate
/// type rather than a key: a pull does not go through SDL at all, so it
/// needs no window and works under `--headless`, which is where the
/// scripted runs that would want one live.
struct scripted_pull {
  std::string id;
  std::uint64_t frame{};
  bool done{false};
};

/// One `--watch` subject: an offset in the program's data segment, and
/// how wide the value there is.
struct watch_point {
  std::uint16_t offset{};
  unsigned width{1};
};

/// What a watch has already said, per segment.
///
/// Per segment rather than one running value, because DS at a frame
/// boundary is not always the program's data segment and a watch that
/// forgot which segment it last read would alternate: the bytes under
/// some other segment differ from the program's, so they print, and then
/// the program's differ from those and print again — two lines a frame,
/// neither of them about the thing being watched. Remembering per
/// segment turns each of those into one line the first time it is seen
/// and silence after, which leaves the log saying exactly what a watch is
/// for: when the watched values moved, and to what.
///
/// Bounded because nothing here should be able to grow without limit on
/// what a program does; a run that ends frames in more segments than this
/// starts forgetting the oldest, and the only cost of forgetting is a
/// line that says again what it said before.
struct watch_log {
  struct entry {
    std::uint16_t segment{};
    std::vector<std::uint16_t> values;
  };
  static constexpr std::size_t max_segments = 64;
  std::vector<entry> seen;
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
/// What `--journal-ocr` means when nobody said otherwise: the program to
/// run. A build that carries its own engine takes this value as "use the
/// one you carry", because a player who typed nothing did not ask for a
/// program (`tesseract_linked_ocr.h`).
constexpr std::string_view default_journal_ocr = "tesseract";

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
  std::vector<scripted_pull> pulls;
  std::vector<watch_point> watches;
  std::vector<std::string> seams;
  bool list_seams{false};

  /// The VFS door (M5-D2, #170), against the directory this host was
  /// pointed at. Applied *after* the run, so `--vfs-list` says what is
  /// in `\\SAVE\\` once the game has saved rather than before it started.
  bool list_vfs{false};
  std::vector<std::string> vfs_gets;
  std::vector<std::string> vfs_removes;
  /// Documents the player presents (M5-D3, #171), as paths on this
  /// machine's own filesystem — not on the emulated one. A code wheel
  /// lives wherever a person keeps their PDFs, which is very often not
  /// inside the game directory.
  std::vector<std::string> documents;

  /// The journal's ingestion (M5-E3, #174). `journal` is the document to
  /// read the entries out of; `journal_store` is where the text goes, or
  /// empty for this platform's default; `journal_ocr` is the engine, or
  /// `none`; `journal_probe` adds the synthetic edition and its fixture
  /// engine for a check that has no real document to use.
  std::string journal;
  std::string journal_store;
  std::string journal_ocr{default_journal_ocr};
  bool journal_probe{false};
  machine::speed_preset speed{machine::default_speed};

  /// Where the speaker's level starts, and whether it starts latched to
  /// silence (#148). Two things and not one, for the reason every mixer
  /// ever built has them as two: mute is a latch that can be lifted, and
  /// lifting it should give back the level that was there rather than
  /// some level the player has to find again. The gain the audio thread
  /// applies is `muted ? 0 : volume`.
  ///
  /// One is "what the machine made" and this host does not go above it;
  /// `audio_gain.h` says why.
  float volume{1.0F};
  bool muted{false};

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

  /// `--record-every`: take a checkpoint every this many frames rather
  /// than every one. One — a checkpoint a frame — is what `--record`
  /// alone has always done and is right for a run a person is pointing
  /// at a problem; the committed session library uses a sparser one
  /// (tests/sessions/README.md). A checkpoint hashes every byte of RAM,
  /// so the cadence is most of what a recording *costs* to make as well
  /// as most of what it costs to store, and a game session at one a
  /// frame is neither affordable to record nor small enough to commit.
  ///
  /// Sparse never means "and not the interesting moments": a frame in
  /// which a key was posted is checkpointed whatever the cadence says,
  /// and so is the frame the run ends on.
  std::uint64_t record_every{1};

  /// Where `--dump` writes. Empty when it was not asked for; the two
  /// files are this plus `.ppm` and `.wav`.
  std::string dump_prefix;

  bool trace{false};

  /// `--automap-store`: keep the automap's exploration beside the save
  /// (M5-E2c, #173). Off by default — see the usage block at the top of
  /// this file for why writing into a player's game directory is asked
  /// for rather than assumed.
  bool automap_store{false};

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

/// `ID@FRAME`, into a scripted pull (#161). The same shape as
/// `parse_press`, and split on the last `@` for the same reason — a seam
/// id is kebab-case and cannot contain one, but making that a fact this
/// parser depends on would be free only until it was not.
[[nodiscard]] bool parse_pull(std::string_view spec, scripted_pull& out) {
  const std::size_t at = spec.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == spec.size()) {
    return false;
  }
  const std::string_view digits = spec.substr(at + 1);
  std::uint64_t frame = 0;
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed = std::from_chars(first, last, frame);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.id = std::string(spec.substr(0, at));
  out.frame = frame;
  return true;
}

/// `OFF[:WIDTH]`, hexadecimal offset, into a watch point. False on
/// anything that is not that.
///
/// Hexadecimal without a `0x`, because every offset a watch is pointed at
/// comes off the same fact table the seams are written from
/// (machine/seam.h) and those are written in hex. A width is 1 or 2 —
/// a byte or a word — and nothing else, because there is no third thing
/// a data offset in a 16-bit program means.
[[nodiscard]] bool parse_watch(std::string_view spec, watch_point& out) {
  std::string_view digits = spec;
  unsigned width = 1;
  const std::size_t colon = spec.rfind(':');
  if (colon != std::string_view::npos) {
    const std::string_view tail = spec.substr(colon + 1);
    if (tail == "1") {
      width = 1;
    } else if (tail == "2") {
      width = 2;
    } else {
      return false;
    }
    digits = spec.substr(0, colon);
  }
  if (digits.empty() || digits.size() > 4) {
    return false;
  }
  std::uint16_t offset = 0;
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed =
      std::from_chars(first, last, offset, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.offset = offset;
  out.width = width;
  return true;
}

/// Read the watched values out of RAM and print them if they moved.
///
/// Offsets are resolved against the processor's DS, and not against
/// `seam_engine::image_base()`, because a global is where the program's
/// own code says it is: this program unpacks itself, and its data
/// segment is nowhere near the image the loader placed. That is the same
/// anchor the seams read their globals through (seam_cheats.cpp), so an
/// offset that means something to one means the same thing to the other.
///
/// `memory_map::ram()` and not `machine::read_memory()`: this is not a
/// bus cycle. A watch that took one would latch an EGA plane or make an
/// open-bus notice of its own, and a run whose log a watch had written
/// into could not be used to say the run was clean.
void print_watch(machine::machine& box, const std::vector<watch_point>& watches,
                 watch_log& log, std::uint64_t frame_index) {
  const std::span<const std::uint8_t> ram = box.memory().ram();
  const std::uint16_t ds = box.processor().regs()[cpu::sreg::ds];
  const auto base = static_cast<std::uint32_t>(ds) * 16U;

  std::vector<std::uint16_t> values;
  values.reserve(watches.size());
  for (const watch_point& point : watches) {
    std::uint16_t value = 0;
    for (unsigned byte = 0; byte < point.width; ++byte) {
      const std::uint32_t at = base + point.offset + byte;
      const std::uint8_t got = at < ram.size() ? ram[at] : 0U;
      value = static_cast<std::uint16_t>(
          value | (static_cast<unsigned>(got) << (8U * byte)));
    }
    values.push_back(value);
  }

  const auto found =
      std::ranges::find(log.seen, ds, &watch_log::entry::segment);
  if (found != log.seen.end()) {
    if (found->values == values) {
      return;
    }
    found->values = values;
  } else {
    if (log.seen.size() >= watch_log::max_segments) {
      log.seen.erase(log.seen.begin());
    }
    log.seen.push_back({.segment = ds, .values = values});
  }

  std::printf("amberfolio: watch frame=%06llu ds=%04X",
              static_cast<unsigned long long>(frame_index), ds);
  for (std::size_t i = 0; i < watches.size(); ++i) {
    std::printf(watches[i].width == 2 ? " %04X=%04X" : " %04X=%02X",
                watches[i].offset, values[i]);
  }
  std::printf("\n");
  std::fflush(stdout);
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
    } else if (arg == "--pull" && i + 1 < argc) {
      scripted_pull pull;
      if (!parse_pull(argv[++i], pull)) {
        std::fprintf(stderr,
                     "amberfolio: --pull wants ID@FRAME, as in"
                     " cheat-kill-all@600\n");
        return opts;
      }
      opts.pulls.push_back(std::move(pull));
    } else if (arg == "--watch" && i + 1 < argc) {
      watch_point point;
      if (!parse_watch(argv[++i], point)) {
        std::fprintf(stderr,
                     "amberfolio: --watch wants a hexadecimal data-segment "
                     "offset, optionally :1 or :2 for its width, as in 6AAD "
                     "or 6AAD:2\n");
        return opts;
      }
      opts.watches.push_back(point);
    } else if (arg == "--volume" && i + 1 < argc) {
      // Percent, because that is what a person means by a volume and
      // because an integer 0-100 has no rounding to argue about. Refused
      // rather than clamped: 150 is a request this host will not honour
      // (it never amplifies), and quietly turning it into 100 would be a
      // wrong answer given silently.
      std::uint64_t percent = 0;
      if (!parse_count(argv[++i], percent) || percent > 100) {
        std::fprintf(stderr,
                     "amberfolio: --volume wants a percentage from 0 to"
                     " 100\n");
        return opts;
      }
      opts.volume = static_cast<float>(percent) / 100.0F;
    } else if (arg == "--mute") {
      opts.muted = true;
    } else if (arg == "--seam" && i + 1 < argc) {
      opts.seams.emplace_back(argv[++i]);
    } else if (arg == "--vfs-list") {
      opts.list_vfs = true;
    } else if (arg == "--vfs-get" && i + 1 < argc) {
      opts.vfs_gets.emplace_back(argv[++i]);
    } else if (arg == "--vfs-remove" && i + 1 < argc) {
      opts.vfs_removes.emplace_back(argv[++i]);
    } else if (arg == "--document" && i + 1 < argc) {
      opts.documents.emplace_back(argv[++i]);
    } else if (arg == "--journal" && i + 1 < argc) {
      opts.journal = argv[++i];
    } else if (arg == "--journal-store" && i + 1 < argc) {
      opts.journal_store = argv[++i];
    } else if (arg == "--journal-ocr" && i + 1 < argc) {
      opts.journal_ocr = argv[++i];
    } else if (arg == "--journal-probe") {
      opts.journal_probe = true;
    } else if (arg == "--seams") {
      opts.list_seams = true;
    } else if (arg == "--trace") {
      opts.trace = true;
    } else if (arg == "--automap-store") {
      opts.automap_store = true;
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
    } else if (arg == "--record-every" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.record_every) ||
          opts.record_every == 0) {
        std::fprintf(stderr,
                     "amberfolio: --record-every wants a positive frame"
                     " count\n");
        return opts;
      }
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
        "                                      [--pull ID@FRAME]\n"
        "                                      [--steps N]"
        " [--until TICKS] [--dump PREFIX] [--dump-every N]\n"
        "                                      [--trace]"
        " [--watch OFF[:N]]\n"
        "                                      [--seam ID] [--seams]\n"
        "                                      [--vfs-list]"
        " [--vfs-get PATH] [--vfs-remove PATH]\n"
        "                                      [--document PATH]\n"
        "                                      [--record FILE]"
        " [--record-every N] [--replay FILE]\n"
        "                                      [--speed xt|turbo|at|386]\n"
        "                                      [--fast N|max]\n"
        "                                      [--volume 0-100] [--mute]\n"
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

  // `--headless` opens no audio device, so there is no level for either
  // of these to be the level of. Refused on the same reasoning as
  // `--fast` above, and with one extra: `--dump`'s WAV is written before
  // the gain in any case, so a headless run that accepted `--mute` would
  // still write a tone — an option that appeared to do nothing at all.
  if (opts.headless && (opts.muted || opts.volume != 1.0F)) {
    std::fprintf(stderr,
                 "amberfolio: --volume and --mute need an audio device;"
                 " --headless opens none\n");
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

  // Two of the journal's three companions are about an *ingestion*, so
  // without one there is nothing for them to be about. Refused rather
  // than ignored, on the same reasoning as `--dump-every` below.
  //
  // `--journal-store` used to be the third, and M5-E4 (#175) is why it is
  // not any more: the reader reads that file on every run, so saying
  // where it is means something with no ingestion in sight. The note that
  // used to be here said reading was #175's; it is, and this is it.
  if (opts.journal.empty() &&
      (opts.journal_probe || opts.journal_ocr != default_journal_ocr)) {
    std::fprintf(stderr,
                 "amberfolio: --journal-ocr and"
                 " --journal-probe need --journal, whose ingestion they"
                 " are about\n");
    return opts;
  }

  // The cadence is a property of the recording being made, so without a
  // recording there is nothing for it to be a property of. Refused rather
  // than ignored, on the same reasoning as `--dump-every` above.
  if (opts.record_every != 1 && opts.record_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --record-every needs --record, whose"
                 " checkpoints it spaces\n");
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
    } else if (!opts.pulls.empty()) {
      also = "--pull";
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

/// Resolve a raw path the way a program's own INT 21h call would, and
/// say so when it does not resolve.
///
/// The host never decides what a path means: `canonicalize_host_path()`
/// is the one place DOS short-name rules live, separator included
/// (machine/vfs.h), and a host that folded case or translated a
/// separator itself would be a second implementation of the rule that
/// says whether two callers are looking at the same file. The ABI's own
/// VFS door makes the identical call for the identical reason — and the
/// separator half of that rule moved into core precisely because this
/// flag became its second caller.
[[nodiscard]] bool resolve_vfs_path(const std::string& raw,
                                    machine::dos_path& out) {
  const machine::vfs_result<machine::dos_path> where =
      machine::canonicalize_host_path(
          std::span<const char>(raw.data(), raw.size()));
  if (!where.ok() || where.value.is_root()) {
    std::fprintf(stderr,
                 "amberfolio: vfs %s is not a path a file can live"
                 " at\n",
                 raw.c_str());
    return false;
  }
  out = where.value;
  return true;
}

/// `path`, spelled the way core spells it — so a listing line and a
/// `--trace` line about the same file are the same characters
/// (machine/report.h).
[[nodiscard]] std::string spell_vfs_path(const machine::dos_path& path) {
  std::array<char, machine::dos_path_capacity> text{};
  static_cast<void>(machine::format_dos_path(path, text));
  return {text.data()};
}

/// `--vfs-list`, `--vfs-get` and `--vfs-remove`, in that order, after the
/// run. See this file's option notes for what each one is for and why
/// `--vfs-get` prints a digest rather than bytes.
void report_vfs(machine::filesystem& files, const options& opts) {
  if (opts.list_vfs) {
    // One walk, filling a listing, rather than an index at a time: a
    // tree walk per row is quadratic on top of an `entry_at()` that is
    // already quadratic, and on a real installation it does not finish
    // (machine/vfs.h has the measurement).
    std::vector<machine::tree_file> found(machine::tree_file_count(files));
    const std::size_t count = machine::tree_files(files, found);
    std::fprintf(stderr, "amberfolio: vfs %llu file(s)\n",
                 static_cast<unsigned long long>(count));
    for (std::size_t i = 0; i < count && i < found.size(); ++i) {
      std::fprintf(stderr, "amberfolio: vfs %s %u\n",
                   spell_vfs_path(found[i].path).c_str(), found[i].size);
    }
  }

  for (const std::string& raw : opts.vfs_gets) {
    machine::dos_path where;
    if (!resolve_vfs_path(raw, where)) {
      continue;
    }
    const machine::vfs_result<machine::file_stat> seen = files.stat(where);
    if (!seen.ok() || seen.value.is_directory) {
      std::fprintf(stderr, "amberfolio: vfs %s is not a file here\n",
                   spell_vfs_path(where).c_str());
      continue;
    }
    // Every byte through the read, hashed as it comes, and nothing kept:
    // a player's file has no business in a log, and the digest of what
    // came back is the claim worth making about a read anyway.
    std::vector<std::uint8_t> bytes(seen.value.size);
    const machine::vfs_result<std::uint32_t> read =
        machine::read_file(files, where, bytes);
    if (!read.ok() || read.value != seen.value.size) {
      std::fprintf(stderr, "amberfolio: vfs %s could not be read whole\n",
                   spell_vfs_path(where).c_str());
      continue;
    }
    const sha256_digest digest = sha256(bytes);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    std::fprintf(stderr, "amberfolio: vfs %s %u sha256=%s\n",
                 spell_vfs_path(where).c_str(), read.value, hex.data());
  }

  for (const std::string& raw : opts.vfs_removes) {
    machine::dos_path where;
    if (!resolve_vfs_path(raw, where)) {
      continue;
    }
    // Said before it is done, and said plainly. This host's filesystem
    // is a directory on the player's disk, not a sandbox, and a flag
    // that reads like a test fixture is exactly the one somebody runs on
    // a real installation.
    std::fprintf(stderr, "amberfolio: vfs deleting %s from %s\n",
                 spell_vfs_path(where).c_str(), opts.root.string().c_str());
    const machine::vfs_error why = files.unlink(where);
    if (why == machine::vfs_error::none) {
      std::fprintf(stderr, "amberfolio: vfs %s deleted\n",
                   spell_vfs_path(where).c_str());
    } else {
      std::fprintf(stderr, "amberfolio: vfs %s not deleted (%s)\n",
                   spell_vfs_path(where).c_str(), machine::vfs_error_name(why));
    }
  }
}

/// Present `digest` to the engine and say what it turned out to be.
///
/// Split out of `present_document` below because the journal's ingestion
/// (#174) has already read and hashed the file it is about to read the
/// insides of, and hashing it a second time to say the same sentence
/// would be a second answer that could differ from the first.
void present_digest(machine::machine& box, const sha256_digest& digest) {
  std::array<char, sha256_digest::text_length + 1> hex{};
  static_cast<void>(format_hex(digest, hex));

  const machine::document_edition* known = box.seams().present_document(digest);
  if (known != nullptr) {
    std::fprintf(stderr, "amberfolio: document %.*s (%s) sha256=%s\n",
                 static_cast<int>(known->name.size()), known->name.data(),
                 machine::document_kind_name(known->kind), hex.data());
    return;
  }
  // Reported, not guessed (machine/document.h). The fingerprint goes on
  // the line because it is the thing somebody can act on: it is what an
  // entry in the table is made of.
  std::fprintf(stderr,
               "amberfolio: document unrecognized sha256=%s - no gate is"
               " satisfied by it\n",
               hex.data());
}

/// Read `path` off *this* machine's filesystem, hash it, and present it
/// to the engine (M5-D3, #171).
///
/// Streamed through a stack buffer rather than read whole: a document is
/// a PDF and a PDF can be tens of megabytes, and nothing about hashing
/// one needs it all in memory at once — the same argument
/// `machine::fingerprint_file` makes for a file on the emulated disk.
///
/// The bytes are hashed and dropped. This host never keeps a document,
/// never parses one, and never writes one anywhere (PLAN.md §2, §6): a
/// possession gate is over bytes, and that is the whole of it.
void present_document(machine::machine& box, const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "amberfolio: document %s could not be read\n",
                 path.c_str());
    return;
  }
  sha256_hasher hasher;
  std::array<char, std::size_t{64} * 1024> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = file.gcount();
    if (got <= 0) {
      break;
    }
    hasher.update(std::span<const std::uint8_t>(
        // The one place this host looks at a document's bytes, and it
        // hands every one of them straight to the hasher.
        reinterpret_cast<const std::uint8_t*>(buffer.data()),
        static_cast<std::size_t>(got)));
  }
  present_digest(box, hasher.finish());
}

#if AMBERFOLIO_HAVE_LINKED_TESSERACT
/// Where a linked build looks for `eng.traineddata`.
///
/// Beside the executable first, because that is where a packaged build
/// would put it and a player's copy must not depend on a path from the
/// machine it was built on; then the build tree's own fetched copy, which
/// is what a developer has and what CMake compiled in.
[[nodiscard]] std::string linked_tessdata_path() {
  std::error_code why;
  const char* base = SDL_GetBasePath();
  if (base != nullptr) {
    std::filesystem::path beside = std::filesystem::path(base) / "tessdata";
    if (std::filesystem::exists(beside / "eng.traineddata", why)) {
      return beside.string();
    }
  }
  return AMBERFOLIO_TESSDATA_DIR;
}
#endif

/// Where a journal's text lives when `--journal-store` did not say.
///
/// The per-user data directory this platform keeps application data in.
/// Empty if this platform will not say where that is, in which case the
/// host asks for `--journal-store` rather than picking somewhere.
[[nodiscard]] std::string journal_store_default_path() {
  // SDL's own answer, which is the right one on all three desktops and
  // is one call rather than three `#ifdef`s that would each be wrong on
  // somebody's machine: `%APPDATA%\\amberfolio\\` on Windows,
  // `~/Library/Application Support/amberfolio/` on macOS, and
  // `$XDG_DATA_HOME/amberfolio/` on Linux. It creates the directory, and
  // it is where M6's configuration will live too -- #174's "beside the
  // config", written down before there is a config to be beside.
  //
  // No organization, because there is no organization: an empty one
  // leaves the application's own directory directly under the platform's
  // data root, which is what a single-application project should write.
  char* where = SDL_GetPrefPath("", "amberfolio");
  if (where == nullptr) {
    return {};
  }
  std::string path(where);
  SDL_free(where);
  path += host::journal_store_filename;
  return path;
}

/// Where the journal's text is, for this run: `--journal-store` if it was
/// given, and this platform's per-user data directory otherwise.
[[nodiscard]] std::string journal_store_path(const options& opts) {
  return opts.journal_store.empty() ? journal_store_default_path()
                                    : opts.journal_store;
}

/// Read a journal store that is already there, for a run that is not
/// ingesting one (M5-E4, #175).
///
/// An ingestion happens once; the reading happens for ever after. So a run
/// with `--seam journal` and no `--journal` still has to find the text a
/// previous run wrote, and this is where it does.
///
/// Every outcome is a sentence and none of them stops the run, because a
/// player whose journal could not be read still asked to play. A file that
/// is not there at all is the ordinary case for somebody who has never
/// ingested one, and it says so once rather than leaving the reader to
/// announce it from inside the game later.
void load_journal_store(const options& opts, host::journal_store& store) {
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: journal this platform does not say where"
                 " per-user data lives; say --journal-store PATH\n");
    return;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr,
                 "amberfolio: journal store %s is not there yet - the"
                 " reader has nothing to show until --journal reads one\n",
                 path.c_str());
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  if (const host::journal_trouble why = store.parse(text);
      why != host::journal_trouble::none) {
    // Left alone rather than used: whatever that file is, it is not this
    // build's, and half a transcription is worse than none.
    store.clear();
    std::fprintf(stderr, "amberfolio: journal store %s - %s\n", path.c_str(),
                 host::journal_trouble_name(why));
    return;
  }
  std::fprintf(stderr,
               "amberfolio: journal store %s entries=%zu corrections=%zu"
               " seen=%zu\n",
               path.c_str(), store.size(), store.corrections(),
               store.seen().size());
}

/// Write the store back, for the log's sake (M5-E4b, #222).
///
/// Ingestion writes this file too, and writes it once at the end of a job
/// that has just done the expensive part. This is the other writer: the
/// log changes while a game is being played, so it is written when the
/// machine says it moved and not on a timer.
void save_journal_store(const options& opts, const host::journal_store& store) {
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    return;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  const std::string text = store.serialize();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  if (!file) {
    std::fprintf(stderr, "amberfolio: journal store %s could not be written\n",
                 path.c_str());
  }
}

/// Ingest `opts.journal` (M5-E3, #174): present it, follow its entries,
/// read them, and keep the text.
///
/// The whole file is read into memory, which is what the extractor wants
/// (`host/journal_extract.h` says why) and what a browser has anyway.
/// Every failure here is a sentence and a return: an ingestion that goes
/// wrong leaves the run alone, because a player who asked to read their
/// journal and could not still asked to play.
void ingest_journal(machine::machine& box, const options& opts,
                    host::journal_store& store) {
  std::ifstream file(opts.journal, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "amberfolio: journal %s could not be read\n",
                 opts.journal.c_str());
    return;
  }
  const std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  const std::span<const std::uint8_t> document(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());

  host::journal_ingester ingester(opts.journal_probe
                                      ? host::journal_probe_table()
                                      : host::known_journals());
  const host::journal_trouble opened = ingester.begin(document);

  // The same sentence `--document` would have printed, off the digest
  // this already computed: a journal is a document, and presenting it is
  // what satisfies a journal-gated seam.
  present_digest(box, ingester.fingerprint());

  if (opened != host::journal_trouble::none) {
    std::fprintf(stderr, "amberfolio: journal unrecognized sha256=%s - %s\n",
                 ingester.fingerprint_hex().c_str(),
                 host::journal_trouble_name(opened));
    return;
  }
  std::fprintf(stderr, "amberfolio: journal %.*s entries=%zu\n",
               static_cast<int>(ingester.edition()->name.size()),
               ingester.edition()->name.data(), ingester.entries());

  // The engine, and what it is. Each way this can go is said out loud:
  // the fixture the probe installs, the engine built into this binary,
  // the player's own installed one, or none — and "none" is a sentence
  // rather than a silence, because a store with no text in it and no
  // explanation is the failure a player finds out about last
  // (`tesseract_ocr.h`).
  host::journal_probe_ocr fixture;
  sdl::tesseract_ocr tesseract(opts.journal_ocr);
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
  // A build that carries its own engine uses it, because a player who has
  // installed nothing is the reason it was carried (#216). Saying
  // `--journal-ocr PATH` still reaches for a program: a player with a
  // newer engine than the one this was built against should be able to
  // ask for it.
  sdl::tesseract_linked_ocr linked(linked_tessdata_path());
#endif
  host::journal_ocr* engine = nullptr;
  if (opts.journal_ocr == "none") {
    std::fprintf(stderr,
                 "amberfolio: journal no engine asked for - the entries"
                 " will be read and no text kept\n");
  } else if (opts.journal_probe) {
    engine = &fixture;
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
  } else if (opts.journal_ocr == default_journal_ocr && linked.available()) {
    engine = &linked;
#endif
  } else if (tesseract.available()) {
    engine = &tesseract;
  } else {
    std::fprintf(stderr,
                 "amberfolio: journal no engine - '%s' did not answer;"
                 " install it or say --journal-ocr PATH\n",
                 opts.journal_ocr.c_str());
  }
  if (engine != nullptr) {
    std::fprintf(stderr, "amberfolio: journal engine %.*s\n",
                 static_cast<int>(engine->engine().size()),
                 engine->engine().data());
  }

  // Where the text goes, and what is already there. Read first, so a
  // correction a player made survives this ingestion — which is the
  // whole reason the store is read at all rather than written fresh.
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: journal this platform does not say where"
                 " per-user data lives; say --journal-store PATH\n");
    return;
  }
  store.clear();
  if (std::ifstream existing(path, std::ios::binary); existing) {
    const std::string text((std::istreambuf_iterator<char>(existing)),
                           std::istreambuf_iterator<char>());
    if (const host::journal_trouble why = store.parse(text);
        why != host::journal_trouble::none) {
      // Refused rather than overwritten: whatever that file is, it is
      // not this build's, and a player's transcription is not something
      // to write over on a guess.
      std::fprintf(stderr, "amberfolio: journal store %s - %s\n", path.c_str(),
                   host::journal_trouble_name(why));
      return;
    }
  }

  const host::journal_ingest_report report = ingester.run(engine, store);
  std::fprintf(stderr,
               "amberfolio: journal entries=%u extracted=%u recognized=%u\n",
               report.entries, report.extracted, report.recognized);
  if (report.first_trouble != host::journal_trouble::none) {
    // The section as well as the number, because three of them number
    // from their own bases and "entry 4" would name three things (#218).
    std::fprintf(stderr, "amberfolio: journal %s %u: %s\n",
                 machine::journal_kind_name(report.first_failure.kind),
                 static_cast<unsigned>(report.first_failure.number),
                 host::journal_trouble_name(report.first_trouble));
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::string serialized = store.serialize();
  out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
  out.flush();
  if (!out) {
    std::fprintf(stderr, "amberfolio: journal store %s could not be written\n",
                 path.c_str());
    return;
  }
  std::array<char, sha256_digest::text_length + 1> hex{};
  static_cast<void>(format_hex(store.fingerprint(), hex));
  // The fingerprint, and not a word of what is in it: a store is a
  // player's own document read off a player's own copy, and a hash names
  // it without carrying any of it (`host/journal_store.h`).
  std::fprintf(stderr,
               "amberfolio: journal store %s entries=%zu corrections=%zu"
               " sha256=%s\n",
               path.c_str(), store.size(), store.corrections(), hex.data());
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
  // The host services a seam may call out to (M5-D1, #169). Attached
  // before anything is loaded and never detached: it is wiring, like an
  // attached device, and it changes nothing at all about a run with
  // every seam off — no handler runs, so nothing calls out. The same
  // object the web host attaches (hosts/common), so a callout means the
  // same thing on both.
  host::host_services services;
  box.seams().set_host(&services);
  // And the one thing that door drives (M5-E2c, #173). Enabled before
  // the filesystem is attached would be too early — `attach()` reads the
  // working table — so the wiring is here and the attach is after the
  // disk is mounted, below.
  services.automap().enable(opts.automap_store);
  log.set_automap_store(&services.automap());
  // And the other thing that door drives (M5-E4, #175): the text the
  // journal reader is answered out of. It lives here, for the whole run,
  // because an ingestion is one moment and the reading is every moment
  // after it — `ingest_journal` fills this object rather than one of its
  // own, and a run with no `--journal` reads what a previous one wrote.
  host::journal_store journal_text;
  services.set_journal_store(&journal_text);
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
  // And now there is a disk to read the exploration sidecar off (M5-E2c,
  // #173). Before the program is loaded, so a panel opened in the first
  // seconds of a run already has last night's map in it. A no-op unless
  // `--automap-store` asked for it.
  services.automap().attach(box);

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
  // The documents the player presented, before the seams that may be
  // gated on them (#171). Before, and not after, so that a gated seam's
  // very first `enable()` sees the gate satisfied and its startup line
  // says `armed` rather than saying `inert` and then quietly changing
  // its mind at the first overlay read.
  for (const std::string& path : opts.documents) {
    present_document(box, path);
  }

  // And the journal, which is a document that is also read inside
  // (#174). Here rather than earlier because it presents itself through
  // the same door, and the same sentence about gates applies to it.
  if (!opts.journal.empty()) {
    ingest_journal(box, opts, journal_text);
  } else if (!opts.journal_store.empty() ||
             std::ranges::find(opts.seams, "journal") != opts.seams.end()) {
    // Only for a run that asked for the reader, or one that said where a
    // store is. A player who did neither is not owed a line about a file
    // they have no use for, and the seam being named is the one signal
    // available before `enable()` — which happens next, and after this so
    // that the store is there the first time a point can be reached.
    //
    // **And `--journal-store` is the other signal, because of `--replay`**
    // (#235). A replay takes its seams from the recording, which is read
    // further down, so at this point `opts.seams` is empty however many
    // seams the run is about to turn on — and the reader would replay
    // with no text and hash differently than it recorded. Somebody who
    // named a store meant it.
    load_journal_store(opts, journal_text);
    // What the store remembers about what the game has said, back into
    // the machine the reader draws from. It is observation there and
    // configuration here, which is why it travels this way round rather
    // than living in either place alone (`machine/journal.h`).
    //
    // These eight lines were here and nowhere else, so the browser did
    // not have them and forgot every `*` on reload (#237). They are
    // `host::restore_journal_log()` now, in `hosts/common`, where both
    // hosts reach them and a test holds the ordering down.
    host::restore_journal_log(box.journal(), journal_text);
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
      // What the seam is gated on, on a line of its own and only when
      // there is one (#171). A person reading a listing needs to know
      // that a seam is waiting on a document *before* they wonder why it
      // is inert — and every seam in this build says `no document`
      // today, so the line would otherwise be noise on every row.
      if (const machine::seam_definition* definition = seams.find(row.id);
          definition != nullptr &&
          definition->gate != machine::document_kind::none) {
        std::fprintf(stderr,
                     "amberfolio: seams %.*s needs the %s (--document)\n",
                     static_cast<int>(row.id.size()), row.id.data(),
                     machine::document_kind_name(definition->gate));
      }
    }
    // And what has been shown to it, so a listing says the whole state
    // it is a listing of.
    for (std::size_t i = 0; i < seams.document_count(); ++i) {
      const machine::document_edition* held = seams.document_at(i);
      std::fprintf(stderr, "amberfolio: seams holding %.*s (%s)\n",
                   static_cast<int>(held->name.size()), held->name.data(),
                   machine::document_kind_name(held->kind));
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
    // Sized by core rather than guessed at: the manifest names the whole
    // disk since #155, so how long a preamble gets is a fact about
    // `dos_path` and how many entries a recording may name, not about
    // this host. The number this used to carry was already too small.
    std::vector<char> preamble(machine::replay_preamble_capacity, '\0');
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

  // --- Dump: the edge list, written as the run makes it (M4-A1, #106) ---
  //
  // `--dump`'s third file. The PPM is the frame the machine composed and
  // the WAV is one *rendering* of the sound it made; this is the sound
  // itself, in the units the machine works in — "at tick T the speaker
  // output became high" — which platform.h calls the canonical audio
  // state and which the WAV's floats explicitly are not. #106 asks the
  // two questions separately, and until now only one of them could be:
  // whether the machine made the right edges at the right ticks is
  // answered by this file, and whether the box filter renders them right
  // is answered by the WAV beside it.
  //
  // Streamed rather than kept and written at the end, because core's log
  // is a bounded drain-per-frame ring with no allocator behind it
  // (platform.h): the loop below empties it every frame, so this file is
  // the whole run's list and the machine never has to hold it. A run that
  // ends badly then leaves the edges it had already made, which is more
  // than a buffer in a dead process would.
  std::ofstream edges;
  std::uint64_t edges_written = 0;
  if (!opts.dump_prefix.empty()) {
    const std::string path = opts.dump_prefix + ".edges";
    edges.open(path, std::ios::trunc);
    if (!edges) {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   path.c_str());
    } else {
      // A header a reader can act on. A tick is meaningless without the
      // rate it is counted at, and this is a file somebody will open in a
      // year with none of this in their head.
      edges << "# amberfolio audio edges\n"
            << "# pit-input-hz " << machine::pit_input_hz << "\n"
            << "# tick level\n";
      box.audio().log_edges(true);
    }
  }

  // Empty the machine's edge log into that file. Called on the machine
  // thread, between slices, which is where the log's producer side lives
  // — nothing here is visible to the audio thread's `render()`, and a
  // reader that perturbed what `render()` saw would be measuring itself.
  const auto drain_edges = [&box, &edges, &edges_written]() {
    if (!edges.is_open()) {
      return;
    }
    std::array<machine::audio_edge, 256> batch{};
    for (;;) {
      const std::size_t got = box.audio().read_edge_log(batch);
      if (got == 0) {
        return;
      }
      for (std::size_t i = 0; i < got; ++i) {
        edges << batch[i].at << ' ' << (batch[i].level ? '1' : '0') << '\n';
      }
      edges_written += got;
    }
  };

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

  // The tick of the last checkpoint written, so that the one taken where
  // the run ends is not a second copy of the one the cadence had just
  // taken. Two checkpoints at one tick would verify, and would say the
  // same thing twice.
  machine::ticks checkpointed = machine::never;

  // Whether this frame delivered an input — a key, or a seam trigger
  // somebody pulled (#161). A sparse cadence must not thin out the
  // moments a recording exists to pin: what a game session is evidence
  // for is that the machine answered *this* keystroke the way it did,
  // and a checkpoint on the far side of the frame that carried one is
  // where that is visible. A pull is the same kind of moment and gets
  // the same treatment.
  bool input_this_frame = false;

  // Pull one seam's trigger, wherever the ask came from — the host key
  // below, or `--pull ID@FRAME` (#161). Recorded as a `pull` line at the
  // machine's own tick, exactly as a keystroke is, and for the same
  // reason: it is something a person did to a running machine at a
  // moment, and a replay that had the seam on but not the pull would
  // reproduce a run in which the cheat never fired (machine/replay.h).
  const auto pull_trigger = [&](std::string_view id) {
    const machine::seam_error why = box.seams().pull(id, box.time());
    if (why != machine::seam_error::none) {
      std::fprintf(stderr, "amberfolio: seam %.*s not pulled (%s)\n",
                   static_cast<int>(id.size()), id.data(), seam_refusal(why));
      return false;
    }
    // What the pull is waiting for, said at the moment it is made. A
    // trigger acts at a CS:IP breakpoint, so "immediately" means "at the
    // next arrival at the point" and nothing else can (machine/seam.h);
    // an inert seam is not even that, and a person who is told neither
    // has a button that did nothing.
    const machine::seam_status row = box.seams().status(id);
    std::fprintf(stderr, "amberfolio: seam %.*s pulled - %s\n",
                 static_cast<int>(id.size()), id.data(),
                 row.armed ? "acts at the next arrival at its point"
                           : "inert; its module is not resident");
    machine::replay_event line{};
    line.kind = machine::replay_line::pull;
    line.at = box.time();
    if (id.size() <= machine::replay_max_id) {
      for (std::size_t i = 0; i < id.size(); ++i) {
        line.id[i] = id[i];
      }
      line.id_length = id.size();
      record_line(line);
    } else if (recording.is_open()) {
      // A recording that quietly left a pull out would be a recording of
      // a run that did not happen. Said out loud instead; the id would
      // have to be longer than any seam in this tree for it.
      std::fprintf(stderr,
                   "amberfolio: seam %.*s pulled but NOT recorded - its id"
                   " is longer than a recording may name\n",
                   static_cast<int>(id.size()), id.data());
    }
    input_this_frame = true;
    return true;
  };

  // The host key's whole job: pull every trigger that is on. One key for
  // however many triggered seams a build carries, because a key per seam
  // is a key this host does not have to spend — an 83-key XT board has
  // only so many codes it never uses, and the toggle surface is where
  // seams are chosen (`--seam`, the page's checkboxes).
  const auto pull_every_trigger = [&]() {
    unsigned pulled = 0;
    for (std::size_t i = 0; i < box.seams().count(); ++i) {
      const machine::seam_status row = box.seams().status(i);
      if (row.state != machine::seam_state::on || !row.trigger) {
        continue;
      }
      if (pull_trigger(row.id)) {
        ++pulled;
      }
    }
    if (pulled == 0) {
      std::fprintf(stderr,
                   "amberfolio: nothing to pull - no seam that takes a"
                   " trigger is on\n");
    }
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
  std::vector<scripted_pull> pulls = opts.pulls;
  watch_log watch_seen;

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  audio_bridge bridge;
  bridge.box = &box;

  // The listening level (#148). `opts` is what the command line said and
  // stays that way; these two are where it is *now*, because F11 and F12
  // move them while the run is going. Neither is machine state, neither
  // is recorded, and nothing in the machine can observe either — a run at
  // 25% is the same run as one at 100%, down to the last edge.
  float volume = opts.volume;
  bool muted = opts.muted;

  const auto say_level = [&volume, &muted]() {
    if (muted) {
      std::fprintf(stderr, "amberfolio: audio muted\n");
    } else {
      std::fprintf(stderr, "amberfolio: audio volume %ld%%\n",
                   std::lround(volume * 100.0F));
    }
  };
  const auto apply_level = [&bridge, &volume, &muted]() {
    bridge.gain.set(muted ? 0.0F : volume);
  };

  // Before the device is opened, so that a run asked to start muted has
  // never played a sample at any other level.
  apply_level();
  if (muted || volume != 1.0F) {
    say_level();
  }

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
    // The journal's log, if the machine has said it moved (M5-E4b, #222).
    // Once a frame rather than once a citation, because a citation is one
    // instruction and a file write is not; and a flag rather than a timer,
    // because most frames have nothing to say.
    if (journal_text.changed()) {
      save_journal_store(opts, journal_text);
      journal_text.clear_changed();
    }
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

    // The scripted pulls this frame owes (#161). Outside the windowed
    // block on purpose: a pull is a call into the engine and not an SDL
    // event, so it needs no window, and a scripted run that wants one is
    // exactly the headless kind.
    for (scripted_pull& pull : pulls) {
      if (!pull.done && pull.frame == frame_index) {
        static_cast<void>(pull_trigger(pull.id));
        pull.done = true;
      }
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
        } else if ((event.type == SDL_EVENT_KEY_DOWN) && !event.key.repeat &&
                   (event.key.scancode == SDL_SCANCODE_F11 ||
                    event.key.scancode == SDL_SCANCODE_F12)) {
          // The host's own two keys (#148), and the only two it takes.
          // An 83-key XT board has ten function keys, so `xt_scancode()`
          // answers 0 for both and the emulated program loses nothing by
          // this; `keymap_test.cpp` pins that assumption rather than
          // leaving it as a belief about a table.
          //
          // Handled during a replay as well, and deliberately: a
          // recording decides what the *machine* did, and how loudly the
          // person watching it wants that played back is not one of
          // those things. Nothing here is posted, recorded or hashed.
          if (event.key.scancode == SDL_SCANCODE_F11) {
            muted = !muted;
          } else if (muted) {
            // Louder, while latched to silence, plainly means "let me
            // hear it" — so the latch lifts and the level it lifts to is
            // the one that was already there. One press, one audible
            // change.
            muted = false;
          } else {
            const auto next = std::ranges::find_if(
                volume_rungs, [&volume](float rung) { return rung > volume; });
            volume = next != volume_rungs.end() ? *next : volume_rungs.front();
          }
          apply_level();
          say_level();
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                   event.key.scancode == SDL_SCANCODE_PAUSE) {
          // The host's third key, and the trigger a person pulls (#161).
          //
          // Pause/Break, on the same argument F11 and F12 were chosen
          // on: an 83-key XT board has no such key at all. Pausing on
          // one was Ctrl and the keypad's Num Lock; the dedicated
          // Pause/Break arrived with the 101-key Enhanced board, where
          // it is the one E1-prefixed sequence, and this machine's wire
          // is set 1 with no prefixes on it. `sdl::xt_scancode()`
          // answers 0 for it, so the emulated program loses nothing —
          // `keymap_test.cpp` pins that, as it does for the other two.
          //
          // Break is also the right word for it: a person interrupting
          // the program from outside is exactly what a debug trigger is.
          // The keypad's `/` and its Enter are the other two keys an XT
          // board has not got, and both were passed over for being
          // inside the cluster this game's movement keys are — a key you
          // can hit by accident mid-fight is the wrong key for a cheat.
          //
          // Unlike F11 and F12 this one reaches the machine, so it is
          // refused during a replay for the same reason a keystroke at
          // the window is: an input the recorded run never had.
          if (replaying) {
            std::fprintf(stderr,
                         "amberfolio: a replay's pulls are the"
                         " recording's\n");
          } else {
            pull_every_trigger();
          }
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
            input_this_frame = true;
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
    drain_edges();
    if (opts.trace) {
      print_overlay_loads(box, overlays_printed);
    }
    if (!opts.watches.empty()) {
      print_watch(box, opts.watches, watch_seen, frame_index);
    }

    // Then the events this slice ran up to, delivered and checked before
    // anything else looks at the machine: a checkpoint is a statement
    // about the machine at a tick, and the display and the speaker are
    // read from it just below.
    if (replaying) {
      static_cast<void>(player.apply(box));
    }

    // A checkpoint every `--record-every` frames, and every frame that
    // posted a key. The boundary is the machine's own — frame ticks off
    // its clock, never the wall — so a recording made on one target names
    // ticks a replay reaches on every other. Taken after the slice and
    // before the frame is presented, which is the moment the run has just
    // finished being somewhere describable.
    if (recording.is_open() &&
        (input_this_frame || frame_index % opts.record_every == 0)) {
      record_line(machine::checkpoint_of(box));
      checkpointed = box.time();
    }
    input_this_frame = false;

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

  // The last slice's edges. The loop drains after each slice, and it
  // leaves by a `break` that is above that drain — so without this, every
  // edge the final slice published would be in the machine's log and in
  // no file, which for a run that ends *because* of what it just did is
  // the part worth reading.
  drain_edges();

  // Where the recording stops, written before SDL comes down so that a
  // run whose teardown goes wrong still leaves a recording saying how far
  // it got. A player that reaches this line verified everything before
  // it; one that runs out of text without it says so.
  if (recording.is_open()) {
    // The frame the run ends on is checkpointed whatever the cadence
    // says, and it is the checkpoint that matters most: it is the one a
    // machine that stopped is described by, and `stopped` on its line is
    // what lets a replaying host run *past* the tick to arrive at it
    // (machine/replay.h). A cadence that happened not to divide the last
    // frame number would otherwise drop it.
    if (box.time() != checkpointed) {
      record_line(machine::checkpoint_of(box));
    }
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

  // What each enabled seam actually did (#131). `armed` says an address
  // was computed; this says a handler ran there. A seam that is on and
  // armed and fired nothing is the failure that reads exactly like
  // success, and the only place a reader can be told about it for free is
  // here, once, at the end of the run it belongs to.
  //
  // A triggered seam (#161) carries two more numbers, and only it does:
  // `reached` is how many times its **addressed** point was arrived at
  // whether or not anybody had asked — the one measurement of how
  // promptly a pull could be served there — and `waited`/`waiting` is
  // what the last pull cost or is still costing.
  //
  // What the row *means* is `machine::seam_reading_of`, in core, and not
  // a decision made here (#163). It used to be made here and again in
  // `host.mjs`, and both got it wrong the same way the moment a seam
  // could act at a point with no address: `fired=1 reached=0` is a
  // success, and both printed "armed and never reached; its point may
  // not be where its facts say" over it. One decision, one spelling, and
  // the page is handed the finished sentence through the ABI — so a
  // reader comparing a browser run with a desktop one is comparing two
  // runs and not two spellings.
  for (std::size_t i = 0; i < box.seams().count(); ++i) {
    const machine::seam_status row = box.seams().status(i);
    if (row.state != machine::seam_state::on) {
      continue;
    }
    std::string extra;
    if (row.trigger) {
      extra += " reached=" + std::to_string(row.reached);
      if (row.waiting) {
        extra += " waiting";
      } else if (row.fired != 0) {
        extra += " waited=" + std::to_string(row.waited);
      }
    }
    // What the row means is core's answer, not this host's (#163).
    const char* say = machine::seam_reading_text(machine::seam_reading_of(row));
    std::fprintf(stderr, "amberfolio: seam %.*s %s fired=%llu%s%s\n",
                 static_cast<int>(row.id.size()), row.id.data(),
                 row.armed ? "armed" : "inert",
                 static_cast<unsigned long long>(row.fired), extra.c_str(),
                 say);
  }

  // And what the seams asked of the host (M5-D1, #169). A line per
  // service that was called, and none for one that was not — which is
  // the whole of what the polled count is for: "it never asked" is a
  // finding, and the difference between it and "it asked and nobody
  // answered" is invisible in any stream (#153). The count is the
  // engine's (machine/seam.h); the tick beside it is this host's own
  // object's, and is the fact only a synchronous implementation can
  // have — the machine's virtual time at the instant of the call.
  for (std::size_t i = 0; i < machine::seam_host_service_count; ++i) {
    const auto which = static_cast<machine::seam_host_service>(i);
    const std::uint64_t calls = box.seams().host_calls(which);
    if (calls == 0) {
      continue;
    }
    std::fprintf(stderr,
                 "amberfolio: host-service %s calls=%llu last=%lu at=%llu\n",
                 machine::seam_host_service_name(which),
                 static_cast<unsigned long long>(calls),
                 static_cast<unsigned long>(box.seams().host_argument(which)),
                 static_cast<unsigned long long>(services.record(which).at));
  }

  // And what the sidecar did with them (M5-E2c, #173). Printed only when
  // it was asked for, and printed even when it did nothing: a store that
  // wrote no file is either a run that explored nothing or a run whose
  // writes were failing, and those are not the same thing.
  if (services.automap().enabled()) {
    const host::automap_store& store = services.automap();
    const char slot = store.slot();
    std::fprintf(stderr,
                 "amberfolio: automap-store writes=%lu reads=%lu slot=%c"
                 " trouble=%s\n",
                 static_cast<unsigned long>(store.writes()),
                 static_cast<unsigned long>(store.reads()),
                 slot != 0 ? slot : '-',
                 host::automap_trouble_name(store.trouble()));
  }

  // The VFS door (M5-D2, #170), over the directory this host was pointed
  // at — the same three operations the ABI gives a browser over its
  // in-memory filesystem, so the two hosts' answers about a disk can be
  // compared rather than described.
  //
  // After the run, because what they exist to answer is what the run
  // left behind. Removals last: a listing that happened after them would
  // be a listing of a disk nobody had.
  report_vfs(files, opts);

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

    // And the edge list's trailer. The count is on the last line as well
    // as in this report so that the file answers "is this all of it?" on
    // its own — a truncated dump and a silent run look identical from the
    // top, and only one of them is a finding about the machine.
    if (edges.is_open()) {
      const std::uint64_t lost = box.audio().edge_log_dropped();
      edges << "# edges " << edges_written << " dropped " << lost << '\n';
      edges.close();
      std::fprintf(stderr,
                   "amberfolio: dump edges=%s.edges count=%llu dropped=%llu\n",
                   opts.dump_prefix.c_str(),
                   static_cast<unsigned long long>(edges_written),
                   static_cast<unsigned long long>(lost));
    }
  }

  // The speaker's two host-pacing symptoms, which until now no host read
  // at all (M4-A1, #106). `platform.h` states the policy for each — an
  // underrun holds the last level and keeps its place, an overrun jumps
  // the cursor forward and throws the backlog away — and a policy no host
  // can report is not a tested policy: a stalled run and a smooth one
  // produced the same silence on stderr.
  //
  // Printed whenever there is something to say, and always under
  // `--verify`, whose job is to say what happened whether or not anything
  // did. A windowed run almost always underruns once at the start,
  // because SDL's device pulls before the machine has settled any virtual
  // time at all; that first one is the shape of a healthy run and not a
  // symptom.
  //
  // `dropped edges` is the third and the loudest: it is the *ring*
  // overflowing, which is sound the machine made and no host ever got.
  {
    const std::uint64_t underruns = box.audio().underruns();
    const std::uint64_t resyncs = box.audio().resyncs();
    const std::uint64_t dropped = box.audio().dropped_edges();
    if (opts.verify || underruns != 0 || resyncs != 0 || dropped != 0) {
      // The listening level joins the line only when it is not unity, so
      // that a default run's report is the line it has always been — and
      // so that a run whose sound was turned down says so where somebody
      // asking "why did I hear nothing" will read it (#148). It is
      // appended rather than inserted for the same reason: the three
      // counters in front of it are what cmake/run-verify-program.cmake
      // matches on.
      std::array<char, 32> level{};
      if (muted) {
        std::snprintf(level.data(), level.size(), " volume=muted");
      } else if (volume != 1.0F) {
        std::snprintf(level.data(), level.size(), " volume=%ld%%",
                      std::lround(volume * 100.0F));
      }
      std::fprintf(stderr,
                   "amberfolio: audio underruns=%llu resyncs=%llu dropped"
                   " edges=%llu%s\n",
                   static_cast<unsigned long long>(underruns),
                   static_cast<unsigned long long>(resyncs),
                   static_cast<unsigned long long>(dropped), level.data());
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
