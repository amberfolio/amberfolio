// SPDX-License-Identifier: AGPL-3.0-only
//
// The core's C ABI — the surface a non-C++ host talks to. It exists for
// the wasm build (PLAN.md §4: "compiled ... via Emscripten to wasm32 with
// a small C ABI for the JS host"), but it is plain C in every build, so a
// native host or a test can call exactly what the browser calls.
//
// Rules for what may be added, unchanged since M0: C linkage only, no
// structs by value, no ownership handed across the boundary that the
// other side has to free, and nothing that can throw. Which of these
// symbols the wasm module actually exports is decided at link time by
// hosts/web/CMakeLists.txt — **a function added here and not added there
// is silently absent from the module**, which is why the smoke check
// asserts the export list.
//
// M2-F4 (#45) grows this from one function to the machine's whole
// platform interface. This comment covers only what is different about
// the C face; **the design lives in core/include/amberfolio/machine/
// platform.h and a host author should read that first.** In one
// paragraph: core produces frames, audio and console bytes and the host
// *pulls* them; the host produces key events, the wall-clock seed and the
// program and *pushes* them; nothing in core ever calls out.
//
//
// The handle
// ----------
//
// `af_machine*` is an opaque, incomplete type. A JS host holds it as a
// number and never looks inside; a C host cannot, because the struct has
// no definition.
//
// **There is one machine per loaded module**, and `af_machine_create()`
// answers null if one already exists. That is not a limitation dressed up
// as a decision — it is what lets the core keep PLAN.md §4's freestanding
// rule that it never allocates. The machine sits in static storage,
// constructed in place by `create` and destroyed by `destroy`; the
// megabyte it contains is .bss, so it costs the wasm bundle nothing and
// the browser page one megabyte of linear memory it was going to need
// anyway. A host that genuinely wants two machines instantiates the
// module twice, which in a browser is one more `createModule()` call.
//
//
// Who owns what
// -------------
//
//   * **The framebuffer and the palette are the core's.** The pointers
//     `af_machine_framebuffer` and `af_machine_palette` return are into
//     the core's own storage, stable for the machine's whole life, and
//     the host neither frees nor writes them. In wasm they are offsets
//     into linear memory that a JS host reads through a typed-array view
//     with no copy on this side.
//
//     One wasm-specific trap, stated here because it will bite otherwise:
//     **a typed-array view is detached when linear memory grows.** The
//     offset stays valid; the `HEAPU8` object you built the view from
//     does not. Re-derive the view after anything that may allocate, or
//     build it fresh each frame — it is a cheap object.
//
//   * **Sample buffers and image buffers are the caller's.** Audio is
//     pulled *into* a buffer the host supplies and the host owns; a
//     program image is read *out of* one. `malloc` and `free` are
//     exported so a JS host can make one — and it frees its own, which is
//     not ownership crossing the boundary, it is each side keeping its
//     own.
//
//   * **Nothing is ever returned that the other side must release.** No
//     `af_*_free`, and there will not be one.
//
//
// Tick quantities are doubles
// ---------------------------
//
// Virtual time is a 64-bit tick count in C++ (machine/clock.h) and it
// crosses this boundary as a `double`. That is not sloppiness: a double
// represents every integer up to 2^53 exactly, which at 1,193,182 Hz is
// about 239 years of emulated runtime, and JS numbers *are* doubles. The
// alternative — `uint64_t` — arrives in JS either as a BigInt (with
// `-sWASM_BIGINT`, and every arithmetic operation on it becomes a BigInt
// operation) or as two i32 halves the host has to reassemble. Both make
// the run loop worse to write and neither buys a year of runtime anybody
// will use.
//
//
// Calling on a stopped machine
// ----------------------------
//
// A stopped machine is a machine that refused to invent something
// (PLAN.md §3), and the host's next job is to *report* that — so nothing
// here becomes unusable when it happens:
//
//   * `af_machine_run_until` takes no steps, costs no virtual time, and
//     answers `AF_STOPPED`. A host that ignores the answer loops
//     harmlessly rather than running the clock away.
//   * The pulls keep working. The last frame is still there, the console
//     ring still drains — and it is the console and the diagnostics that
//     say *why* it stopped, so cutting them off at the stop would throw
//     away the answer.
//   * The pushes are still accepted and simply have no effect on a
//     machine that is not running.
//   * `af_machine_reset` clears the stop, as the RESET line does.
//
// A null handle is not a crash either: every function tolerates one and
// answers `AF_NO_MACHINE`, a null pointer, or zero as its return type
// allows. A JS host whose `create` failed should get an error code, not a
// trap in the middle of its render loop.
//
//
// Threading
// ---------
//
// Everything here is machine-thread only, with the single exception
// platform.h spells out: `af_machine_render_audio` may be called from one
// other thread, concurrently with anything else. Exactly one thread, not
// one at a time. In the browser that thread is usually the main one
// anyway — a wasm module has no shared memory with an AudioWorklet unless
// it opts into threads — so the dev page (#55) pulls samples on the main
// thread and posts them to the worklet, which the contract permits.

#ifndef AMBERFOLIO_ABI_H
#define AMBERFOLIO_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Status codes -----------------------------------------------------
//
// Macros rather than an enum, so that the header stays valid C of any
// vintage and a JS host can copy the numbers without a translation step.
// Zero is success and nothing else is, which is the only convention worth
// having at an ABI.

/// The call did what it says.
#define AF_OK 0u
/// The handle was null, or no machine has been created.
#define AF_NO_MACHINE 1u
/// The machine has stopped and will not run until it is reset. Not an
/// error in the call — an answer about the machine.
#define AF_STOPPED 2u
/// An argument was out of range, and nothing happened.
#define AF_INVALID 3u
/// A real entry point that nothing implements yet. Loud on purpose
/// (PLAN.md §3): a host gets a distinct code rather than a plausible
/// success.
///
/// **Nothing answers this today.** It was the reserved MZ loader's, and
/// M3-F2 (#84) replaced that with `af_machine_load_from_vfs` below. The
/// number stays where it is rather than being reclaimed: these are ABI
/// values, a host may have them written down, and shifting one to close
/// a gap is a change with no benefit and a real cost. It is the code the
/// next reserved entry point should use.
#define AF_UNIMPLEMENTED 4u
/// The machine has no filesystem attached, so there is nothing for a
/// `af_machine_vfs_*` call to be about. Its own code rather than
/// AF_NO_MACHINE, which would be a lie about a machine that exists: a
/// host that forgot `af_machine_attach_reference_devices()` should be
/// told what it forgot.
#define AF_NO_FILESYSTEM 5u
/// The filesystem had nothing left to do the call with: no free entry
/// for another file or directory, no room for the bytes of this one, no
/// handle to open it with. The call did not do what it says, and —
/// unlike `AF_INVALID` — nothing about the argument was wrong.
///
/// Its own code rather than `AF_INVALID` (M4, #158), because those two
/// are the only ways `af_machine_vfs_put` refuses a file and they call
/// for opposite responses. A path no DOS short name can equal is the
/// answer a host wants: a boxed copy carries a PDF, the machine says so,
/// and the run goes on without it. A full filesystem is a *hole in what
/// the machine is running* — the file the program will ask for later is
/// not there — and the host's next move is to stop, or to clear and take
/// a smaller disk. Folded into one code they read alike, and a browser
/// spent M4 reporting seven missing data files in the same sentence as a
/// correctly-ignored PDF.
///
/// A host that only wants "did it work" still writes `!= AF_OK`, which
/// is the whole convention this space has.
#define AF_NO_ROOM 6u

// There is no "a machine already exists" code, because no function can
// return one: `af_machine_create` answers a pointer, and null is the
// whole of what it has to say. A status nothing produces is a status a
// host would write a branch for and never reach.

// --- How a run ended --------------------------------------------------
//
// The values of machine::run_end (machine/report.h), for
// `af_machine_stop_report` below. A host that ended the run itself — a
// step budget, a closed page — says so, and the report prints its reason
// instead of the machine's. Asserted against the C++ enum in abi.cpp.

#define AF_RUN_END_STOPPED 0u
#define AF_RUN_END_STEP_BUDGET 1u
#define AF_RUN_END_TICK_BUDGET 2u
#define AF_RUN_END_HOST_QUIT 3u

// --- Speed presets ----------------------------------------------------
//
// The values of machine::speed_preset, restated for a C caller. Asserted
// against it in abi.cpp, so the two cannot drift apart.

#define AF_SPEED_PC_XT 0u
#define AF_SPEED_TURBO_XT 1u
#define AF_SPEED_AT 2u
#define AF_SPEED_PC_386 3u

/// The largest preset there is, so a caller can validate one without
/// knowing how many there are — and so `af_machine_set_speed` has one
/// name to bound against rather than whichever enumerator happens to be
/// last this month.
#define AF_SPEED_MAX AF_SPEED_PC_386

// --- Key actions ------------------------------------------------------

#define AF_KEY_UP 0
#define AF_KEY_DOWN 1

// --- Seam states ------------------------------------------------------
//
// The values of machine::seam_state (machine/seam.h), for
// `af_machine_seam_state` below, plus one this side adds: the answer for
// an index that names no seam. Asserted against the C++ enum in abi.cpp.

#define AF_SEAM_OFF 0u
#define AF_SEAM_ON 1u
#define AF_SEAM_UNAVAILABLE 2u
#define AF_SEAM_NONE 3u

// --- Version ----------------------------------------------------------

/// The version of the core, packed as 0x00MMmmpp: major in bits 16-23,
/// minor in 8-15, patch in 0-7. One scalar rather than three calls, and
/// directly comparable — the JS side unpacks it the same way AF_VERSION_*
/// does below.
///
/// This is the *linked* version (see amberfolio::linked_version), which is
/// the only kind that means anything across an ABI: a JS host has no
/// headers it was compiled against.
uint32_t af_version(void);

// --- Facts about the machine ------------------------------------------
//
// Constants a host would otherwise hard-code. They are functions and not
// macros because the wasm host has no headers: a browser learns these by
// calling, which is the only way a JS file and a C++ file can be sure
// they agree.

/// Ticks of virtual time in one second — the PIT input clock, 1,193,182
/// Hz. A host paces its run loop with this: 60 frames a second is
/// `af_ticks_per_second() / 60` ticks per frame.
double af_ticks_per_second(void);

/// The framebuffer's dimensions, and how many palette entries there are.
/// 320, 200 and 16 — mode 0Dh, the one video mode this machine has.
uint32_t af_frame_width(void);
uint32_t af_frame_height(void);
uint32_t af_palette_entries(void);

// --- The machine ------------------------------------------------------

/// The opaque handle. Incomplete on purpose: there is nothing in it a
/// host may read.
///
/// `typedef` and not `using`, which clang-tidy would rather have: this
/// header has to compile as C, for the same reason it includes <stdint.h>
/// rather than <cstdint> (see .clang-tidy's note on
/// modernize-deprecated-headers). Silenced on the line rather than in the
/// config, so the check keeps its teeth everywhere else.
// NOLINTNEXTLINE(modernize-use-using)
typedef struct af_machine af_machine;

/// Build the machine, powered on and reset. Null if one already exists or
/// if the build refused for any other reason — see "The handle" above.
af_machine* af_machine_create(void);

/// Destroy it. Null is accepted and ignored, so a host's teardown path
/// needs no check.
void af_machine_destroy(af_machine* box);

/// Attach PLAN.md §3's reference device set — the PIT, the minimal 8259,
/// the EGA and its renderer, the speaker — and install the video (INT
/// 10h) and DOS (INT 21h) service handlers, all in one call.
///
/// `af_machine_create()` deliberately does **not** do this: a freshly
/// created machine is CPU and RAM only, so that an unbacked vector still
/// stops the machine rather than quietly finding a handler nobody asked
/// for (PLAN.md §3's "log, don't fake," exercised at this very boundary
/// by the native test suite). A host that wants a complete, PLAN.md
/// §3-shaped PC — the wasm dev page (#55) is the first one — calls this
/// once, right after `create()`. Idempotent: calling it again on the
/// same machine does nothing and still answers `AF_OK`.
///
/// `AF_NO_MACHINE` for a null handle. There is no failure code for the
/// device wiring itself — every device this attaches claims a distinct,
/// fixed memory window or port range, so nothing here can collide on a
/// freshly created machine.
uint32_t af_machine_attach_reference_devices(af_machine* box);

/// The RESET line (machine::reset). Clears any stop, puts the virtual
/// clock back to zero, blanks the frame, restarts the audio timeline, and
/// throws away queued input and undrained console output. RAM keeps what
/// it held, exactly as a warm boot does.
uint32_t af_machine_reset(af_machine* box);

/// Run until the virtual clock reaches `tick`, or until the machine
/// stops.
///
/// **Absolute, not a duration**, for the reason machine::run() is: a host
/// keeps its own schedule and passes points on it, so the overshoot of
/// one slice — a step is indivisible, so the clock can end slightly past
/// `tick` — never accumulates into drift. The run loop is
///
///     next += af_ticks_per_second() / 60;
///     af_machine_run_until(box, next);
///
/// and not `run_for(oneFrame)`.
///
/// `AF_STOPPED` if the machine stopped, at the start or during the slice;
/// `af_machine_time` then says how far it got.
uint32_t af_machine_run_until(af_machine* box, double tick);

/// The virtual clock, in ticks since the last reset.
double af_machine_time(const af_machine* box);

/// Scheduling steps since the last reset (machine::steps).
///
/// The other axis a run is measured on, and the one that stays comparable
/// when the speed governor does not: "the same stop line at the same
/// step" is the claim the desktop host and this one make to each other
/// (M3-F2, #84), and this is the number in it. A `double` for the reason
/// every tick quantity here is one — see "Tick quantities are doubles"
/// above.
double af_machine_steps(const af_machine* box);

/// Start or stop recording the trace ring (machine/trace.h): the last
/// 256 instructions and 64 service calls, in fixed storage inside the
/// machine — **and** the two high-volume diagnostic streams, service
/// calls and file events, in the log below.
///
/// One switch for both halves because they are one facility: the live
/// stream and the ring dumped at the end are asked for together or not at
/// all, which is what the SDL host's `--trace` does with the same two
/// things (hosts/sdl/src/main.cpp). A host that wanted only one of them
/// would be asking a question no boot log has ever needed the answer to.
///
/// A setting, not state — it survives `af_machine_reset`, exactly as the
/// speed preset does. Off until asked for, at a cost of one branch per
/// step, because a boot runs for hundreds of millions of them.
uint32_t af_machine_set_trace(af_machine* box, int32_t on);

// --- Why the run ended ------------------------------------------------
//
// The diagnostics sink is a C++ interface (machine/diagnostics.h) and
// deliberately does not cross this boundary: it hands out structured
// records held by reference, which is the opposite of everything the
// rules at the top of this file ask for. What a JS host needs instead is
// the *account*, already written — and it needs it to be the same
// account the desktop host prints, because M3's exit criterion is that
// the two agree at the same step (#84).
//
// So the formatting lives in core (machine/report.h) and these hand the
// characters over. A host prints them; it does not parse them. That is
// true of all three: the stop report and the trace report, asked for once
// when a run ends, and the running log below, drained as it fills.

/// Write the stop report into `out`, NUL-terminated, and answer how many
/// characters that was (the terminator not counted).
///
/// `how` is one of the AF_RUN_END_* values: AF_RUN_END_STOPPED when the
/// machine stopped of its own accord, and one of the others when the host
/// ended the run itself. Anything else is `AF_RUN_END_STOPPED`'s
/// behaviour, because a report is not worth refusing to write.
///
/// 512 bytes is enough for any report this can produce; a smaller buffer
/// truncates rather than failing, since a short report is still a report.
uint32_t af_machine_stop_report(const af_machine* box, uint32_t how, char* out,
                                uint32_t max);

/// The same for the trace ring. 32768 bytes is enough for a full one;
/// a machine that was never asked to trace writes one line saying so.
uint32_t af_machine_trace_report(const af_machine* box, char* out,
                                 uint32_t max);

/// Non-zero once the machine has stopped. Sticky until reset.
int32_t af_machine_stopped(const af_machine* box);

/// Why it stopped: the value of machine::stop_reason, with 0 meaning
/// "running". The detail behind it — the opcode, the service, the address
/// — is in the stop report above and in the log below, both of them the
/// same characters the desktop host prints.
uint32_t af_machine_stop_reason(const af_machine* box);

/// Drain up to `max` characters of the running log into the caller's
/// buffer, answering how many were copied.
///
/// The diagnostics stream as text: one line per record, newline-ended,
/// each formatted by `machine::format_diagnostic` so that it is character
/// for character the line the SDL host prints for the same record
/// (machine/log.h, machine/report.h). Notices and seam transitions
/// always; service calls and file events only after
/// `af_machine_set_trace`.
///
/// **Not NUL-terminated** — this is a stream, and the answer is its
/// length. A drain can land mid-line when the buffer is smaller than what
/// is waiting; the remainder is the next drain's first characters, so a
/// host that appends what it reads sees whole lines and loses nothing.
///
/// It survives a stop, like the console ring and for the same reason: it
/// is where the answer is.
uint32_t af_machine_read_log(af_machine* box, char* out, uint32_t max);

/// Characters waiting to be drained, and *lines* the ring had no room
/// for. The machine never blocks on a host that has stopped reading; a
/// line that will not fit is dropped whole and counted, since half a line
/// in a log reads as a fact.
uint32_t af_machine_log_pending(const af_machine* box);
double af_machine_log_dropped(const af_machine* box);

/// Throw away what has not been drained, and the drop count with it.
///
/// A host's call and not `af_machine_reset`'s: the log sits beside the
/// machine rather than inside it and is not machine state — nothing in it
/// is hashed, saved or replayed — so a reset leaves it alone and a host
/// that wants a clean log between runs says so.
uint32_t af_machine_clear_log(af_machine* box);

/// Put the speed governor on one of the AF_SPEED_* presets.
uint32_t af_machine_set_speed(af_machine* box, uint32_t preset);

// --- Frame out --------------------------------------------------------

/// The indexed framebuffer: `af_frame_width() * af_frame_height()` bytes,
/// row-major, each a palette index 0-15. Core-owned, stable for the
/// machine's life, never freed by the host.
const uint8_t* af_machine_framebuffer(const af_machine* box);

/// The palette: `af_palette_entries() * 3` bytes, red, green and blue per
/// entry in that order. Same ownership.
const uint8_t* af_machine_palette(const af_machine* box);

/// How many frames have been completed. A host presents only when this
/// differs from what it last presented; the gap says how many it missed,
/// and missing frames is correct behaviour for a host that cannot keep up
/// (platform.h).
double af_machine_frame_generation(const af_machine* box);

// --- Audio pull -------------------------------------------------------

/// Fill `out` with `frames` mono float samples at `sample_rate`, from the
/// speaker's edge list. `out` is the **caller's** buffer, `frames` floats
/// long.
///
/// Answers how many of those frames came from settled virtual time; the
/// rest, if any, are the held level because the machine has not generated
/// that audio yet. `out` is written in full either way. Zero, and `out`
/// untouched, for a null handle, a null buffer, or a sample rate outside
/// 4000-192000.
///
/// **This is the one function here that may be called off the machine
/// thread**, by exactly one thread. See platform.h's threading contract —
/// it is the subtlest thing in this interface and it is written out in
/// full there.
uint32_t af_machine_render_audio(af_machine* box, float* out, uint32_t frames,
                                 uint32_t sample_rate);

/// Pulls that ran out of settled virtual time, and pulls that had to jump
/// forward to bound latency. Host-pacing symptoms, not machine state: a
/// dev page shows them so "why does it sound wrong" has a number
/// attached.
double af_machine_audio_underruns(const af_machine* box);
double af_machine_audio_resyncs(const af_machine* box);

// --- Input in ---------------------------------------------------------

/// Inject a key event, stamped at the machine's current virtual time.
///
/// `scancode` is an XT (set 1) make code, 1-0x53, **without** the 0x80
/// release bit; `down` is AF_KEY_DOWN or AF_KEY_UP. Nothing else crosses:
/// no ASCII, no modifier mask. Translation and shift state live in core
/// with M2-D8 (#53), because programs of the era read the shift byte out
/// of the BIOS data area directly and it therefore has to be in the
/// machine.
///
/// `AF_INVALID` if the scancode is out of range; `AF_OK` even if the
/// queue was full, with the drop counted inside — a lost keystroke is not
/// a failed call, it is what a full BIOS buffer does.
uint32_t af_machine_post_key(af_machine* box, uint32_t scancode, int32_t down);

// --- The wall clock ---------------------------------------------------

/// Seed the date and time DOS 2Ah/2Ch report: at this moment in virtual
/// time, the wall clock out in the world reads this.
///
/// Every later read is this instant plus the virtual time elapsed since,
/// so it advances, it is deterministic, and a replay reproduces it by
/// seeding the same value at the same tick. The weekday is computed and
/// is not an argument. A host that never calls this gets 1980-01-01
/// 00:00:00 — which is not a fake but what a PC with no clock card gave
/// you. See platform.h for why this is a seed and not a callback.
///
/// `AF_INVALID`, and nothing changed, if the date or time is not real:
/// the year outside 1980-2099, 31 April, 29 February in a common year, an
/// hour past 23.
uint32_t af_machine_set_wall_clock(af_machine* box, uint32_t year,
                                   uint32_t month, uint32_t day, uint32_t hour,
                                   uint32_t minute, uint32_t second,
                                   uint32_t centisecond);

// --- Console output ---------------------------------------------------

/// Drain up to `max` bytes of DOS console output into the caller's
/// buffer, answering how many were copied. There is no text-mode video in
/// this machine and none is planned; this is where INT 21h's console
/// functions write (M2-D7, #52).
///
/// Bytes, not text: what DOS writes is code page 437, and transcoding it
/// is the host's decision.
uint32_t af_machine_read_console(af_machine* box, uint8_t* out, uint32_t max);

/// Bytes waiting to be drained, and bytes the ring had no room for. The
/// machine never blocks on a host that has stopped reading; it drops the
/// newest and counts it.
uint32_t af_machine_console_pending(const af_machine* box);
double af_machine_console_dropped(const af_machine* box);

// --- The filesystem ---------------------------------------------------
//
// The wasm counterpart of the directory the SDL host is pointed at
// (M3-F2, #84). A browser cannot hand the core a directory, so it hands
// it one file at a time, and these are the doors.
//
// **Names are normalized in core, not by the host.** `af_machine_vfs_put`
// and `af_machine_load_from_vfs` take raw text and run it through
// `machine::canonicalize()`, which is the one place DOS short-name rules
// live (machine/vfs.h). A page that decided for itself what `Save1.Dat`
// meant would be a second implementation of the rule that decides whether
// two programs are looking at the same file, and the two would eventually
// disagree.
//
// A name no legal DOS short name can equal is `AF_INVALID`, and that is
// the useful answer rather than a failure: a real game directory has
// files in it that DOS could never have named, and a host wanting to
// report "these were skipped" gets its list from this.
//
// Running out of room is `AF_NO_ROOM` and is the other kind of skip
// entirely (M4, #158) — see that status above. A host reporting a
// skipped list must say which of the two it was, because one is the
// machine working and the other is the disk it was handed being
// incomplete on it.
//
// **A path, not merely a name** (M4, #146). Every `const char*` in this
// section names a *path*: `START.EXE` at the root, `SAVE\SAVE1.DAT` one
// directory down, and `/` is taken as a separator wherever `\` is,
// because what a browser hands a page is `webkitRelativePath` and that is
// the spelling it arrives in. All of that is the *same* rule applied
// component by component in the *same* one place, for the reason the
// paragraph above gives: a host that translated separators itself, or
// invented a flat name for something under `\SAVE\`, would be deciding
// what a path is. The translation stops at this boundary —
// `machine::canonicalize()` keeps DOS's one separator, so nothing about
// what the emulated program may write changes.
//
// Until #146 this door reached the root and no further, and the visible
// consequence was that a browser could start a game and never resume
// one: `\SAVE\` is where a shipped save slot lives, and a host handing
// over an installation dropped it.
//
// Every one of these needs a filesystem, which means
// `af_machine_attach_reference_devices()`; without one they answer
// `AF_NO_FILESYSTEM`.

/// Empty the filesystem: every file gone, every byte reclaimed. What a
/// page calls before taking a second directory from the player.
uint32_t af_machine_vfs_clear(af_machine* box);

/// Put `size` bytes at `path`, replacing whatever was there.
///
/// `path` is NUL-terminated raw text — `START.EXE`, `save1.dat`,
/// `SAVE/Save1.Dat`, `\SAVE\SAVE1.DAT` — and is canonicalized here.
/// **The directories on the way are made as needed**: a host handing an
/// installation over one file at a time is in no position to have made
/// them first, and `machine::filesystem::create()` deliberately will not
/// (a missing parent is exactly what tells `path_not_found` from
/// `file_not_found` for a program that asks).
///
/// `AF_INVALID`, **and nothing made**, for a component that is not a
/// legal DOS short name, a path deeper than `machine::dos_path`'s
/// `max_depth`, the root itself, a component above the leaf that already
/// exists as a *file*, or a leaf that already exists as a *directory*.
/// Every one of those is settled before the first `mkdir`, so a refused
/// path leaves no half-built tree behind.
///
/// `AF_NO_ROOM`, and **not** `AF_INVALID`, when the filesystem runs out
/// — a free entry for the file or a directory above it, bytes for its
/// contents, a handle to open it with. Nothing about the path was wrong;
/// there was nowhere to put it. The two were one code until #158, and
/// the browser that found it could not tell a correctly-refused PDF from
/// seven of its game's data files falling off the end of a full table.
///
/// This is the one answer here that does not leave the filesystem as it
/// found it. The *file* is taken away again, because a half-written
/// `START.EXE` sitting there under the right name is precisely the
/// plausible wrong answer PLAN.md §3 is about — but a directory this
/// call made on the way to it stays. It is empty, it is named exactly
/// what the caller asked for, and a host that reached this is out of
/// room: its next move is `af_machine_vfs_clear`, not another put.
///
/// `AF_NO_FILESYSTEM` when there is no filesystem attached.
uint32_t af_machine_vfs_put(af_machine* box, const char* path,
                            const uint8_t* bytes, uint32_t size);

/// How many entries the root directory holds, and the name and size of
/// one of them. The order is the VFS's own pinned name order
/// (machine/vfs.h), so a listing is the same on every host and in every
/// run.
///
/// The **root**, and only it, even though `af_machine_vfs_put` reaches
/// below it (#146). The consumer is a page offering a player something to
/// boot, and the program of an installation of this era sits at its root;
/// a host that wants to know what it put further down knows, because it
/// put it there. A recursive listing would be a second answer to a
/// question nobody has asked yet.
///
/// `af_machine_vfs_name_at` writes a NUL-terminated name into `out` and
/// answers its length, or zero for an index past the end or a buffer
/// smaller than 13 bytes (`FILENAME.EXT` and its terminator).
uint32_t af_machine_vfs_count(const af_machine* box);
uint32_t af_machine_vfs_name_at(const af_machine* box, uint32_t index,
                                char* out, uint32_t max);
uint32_t af_machine_vfs_size_at(const af_machine* box, uint32_t index);

/// Bytes the filesystem is holding, across every file.
double af_machine_vfs_bytes_used(const af_machine* box);

/// The SHA-256 of `name`, as 64 lowercase hex characters written into
/// `out` and NUL-terminated; answers the length, or zero if the file
/// could not be read or `out` is smaller than 65 bytes.
///
/// A path, canonicalized like every other one here, so a file in a
/// subdirectory is identifiable too — `SAVE/SAVE1.DAT` names the file an
/// `af_machine_vfs_put` of that path wrote.
///
/// The identity of a player's file (PLAN.md §2), and the same digest the
/// desktop host prints at load — one implementation, below both hosts,
/// so that a fingerprint means the same thing wherever it is taken.
uint32_t af_machine_vfs_fingerprint(af_machine* box, const char* name,
                                    char* out, uint32_t max);

// --- Getting a program in ---------------------------------------------

/// Load an MZ executable *from the filesystem*: relocations, PSP, entry
/// state, the lot. `name` is canonicalized here, as everything else in
/// this section is; `command_tail` may be null and is otherwise copied
/// into the PSP verbatim.
///
/// This replaced a reserved `af_machine_load_program(image, size)` that
/// answered `AF_UNIMPLEMENTED` while the loader was still M2-D6 (#51).
/// The loader takes a file off a `filesystem` rather than a buffer, so a
/// buffer-shaped entry point could only have been implemented by staging
/// the bytes under a name of the ABI's own invention — which is exactly
/// the kind of quiet fiction PLAN.md §3 exists to prevent. A host with an
/// image in hand calls `af_machine_vfs_put` and then this.
///
/// `AF_OK`, or `AF_INVALID` with the reason available from
/// `af_machine_load_error()`.
uint32_t af_machine_load_from_vfs(af_machine* box, const char* name,
                                  const char* command_tail);

/// Why the last `af_machine_load_from_vfs` failed: the value of
/// `machine::loader_error`, with 0 meaning the last load succeeded (or
/// that none has been attempted).
///
/// A second call rather than an out-parameter, because a `loader_error`
/// is not a status: `AF_INVALID` already says the call failed, and
/// folding fourteen loader outcomes into this file's status space would
/// make `AF_OK == 0` stop being the only thing a host has to check.
uint32_t af_machine_load_error(const af_machine* box);

// --- Identity and seams (M4-F1 #95, M4-F4 #98) --------------------------
//
// `af_machine_load_from_vfs` also *identifies* the program: it takes the
// file's SHA-256 and tells the seam engine what is running, which is what
// makes the seams below available or not (machine/seam.h). Everything
// here is a configuration call, made between `af_machine_run_until`
// slices and never from inside one — the same rule a host already keeps
// for `af_machine_post_key`.

/// The edition the loaded program is, as a name a host shows, written
/// NUL-terminated into `out`; answers its length. Zero, and nothing
/// written, when no program is loaded, when the edition is not one this
/// build knows (machine/edition.h — the honest "unrecognized" answer, in
/// which case no seam is available), or when `out` is too small.
uint32_t af_machine_edition(const af_machine* box, char* out, uint32_t max);

/// The SHA-256 of the loaded program, as 64 lowercase hex characters,
/// NUL-terminated; answers its length, or zero when no program is loaded
/// or `out` is smaller than 65 bytes. The same digest
/// `af_machine_vfs_fingerprint` takes of the file, kept on the machine as
/// an initial condition of the run (PLAN.md §4).
uint32_t af_machine_program_fingerprint(const af_machine* box, char* out,
                                        uint32_t max);

/// How many seams this build's registry holds, available or not.
uint32_t af_machine_seam_count(const af_machine* box);

/// The id and the one-line description of seam `index`, NUL-terminated
/// into `out`; each answers its length, or zero for an index past the
/// end or a buffer too small.
uint32_t af_machine_seam_id(const af_machine* box, uint32_t index, char* out,
                            uint32_t max);
uint32_t af_machine_seam_about(const af_machine* box, uint32_t index, char* out,
                               uint32_t max);

/// Where seam `index` stands: AF_SEAM_OFF, AF_SEAM_ON, or
/// AF_SEAM_UNAVAILABLE — the last for a seam whose addresses are facts
/// about a different binary, or for any seam before a program is loaded.
/// AF_SEAM_NONE for an index past the end.
uint32_t af_machine_seam_state(const af_machine* box, uint32_t index);

/// Why seam `index` is unavailable, or why an enabled one is not armed
/// (its module is not resident — machine/overlay.h), as the spelling
/// `machine::seam_reason_name` gives it, NUL-terminated into `out`.
/// `none` for a seam that is off, or on and armed. Answers the length, or
/// zero for a bad index or a small buffer.
uint32_t af_machine_seam_reason(const af_machine* box, uint32_t index,
                                char* out, uint32_t max);

/// Whether seam `index` is armed right now — on, and every one of its
/// points placed. Non-zero means yes.
int32_t af_machine_seam_armed(const af_machine* box, uint32_t index);

/// How many times one of seam `index`'s handlers has actually run since
/// it was enabled (`machine::seam_status::fired`). Zero for a seam that
/// is off, for one that has never been reached, and for an index past
/// the end — `af_machine_seam_state` is what tells an index apart.
///
/// **`armed` is a claim about the fact table; this is a claim about the
/// machine** (#131). A point is armed at an address computed from where
/// a module was recorded, so a seam whose module has since moved — or
/// whose offset was never right — answers armed, does nothing, and reads
/// exactly like one that works. `armed` and `fired == 0` after a run
/// that should have fired is a defect a reader can see, and it is the
/// half of fail-closed a browser could not report until this call
/// existed.
///
/// A `double` and not an `int32_t` like `af_machine_seam_armed` above,
/// because the two answer different kinds of question: that one is a
/// predicate and this one is a 64-bit count, which crosses here the way
/// every other count does — `af_machine_log_dropped`,
/// `af_machine_audio_underruns`, `af_machine_frame_generation`. See this
/// file's "Tick quantities are doubles" for why.
///
/// Bookkeeping, not machine state: it lives above the fidelity boundary,
/// is never serialized or hashed, and can only move while a seam is on —
/// so a run with everything off is the run it always was.
///
/// Polled rather than pushed, and deliberately not a `seam_event`: the
/// events on the diagnostics channel are *transitions* (on, off, armed,
/// inert, refused), a fire is not one, and a point in a loop fires
/// hundreds of times a second — a per-fire event would bury the
/// transitions it shares a ring with. A stream also cannot say `fired ==
/// 0`, which is the whole thing this call is for: absence of an event is
/// exactly the silence that reads as success.
double af_machine_seam_fired(const af_machine* box, uint32_t index);

/// Turn the seam called `id` on or off. `AF_OK` if it took; `AF_INVALID`
/// if there is no such seam or it is unavailable, with the reason
/// readable through `af_machine_seam_reason` on that seam.
uint32_t af_machine_seam_enable(af_machine* box, const char* id);
uint32_t af_machine_seam_disable(af_machine* box, const char* id);

// --- Seam triggers (M4, #161) -------------------------------------------
//
// The host -> seam direction. A seam whose definition says so acts only
// when a person asks: the host sets a one-shot latch, the next arrival at
// one of the seam's points runs the handler, and the latch clears
// (machine/seam.h). Everything here is a configuration call like the two
// above — made between `af_machine_run_until` slices, never from inside
// one.

/// Whether seam `index` is pulled rather than left on. Non-zero means a
/// page should give it a **button** and not only a checkbox: a trigger
/// is a different affordance from a toggle, and one shown as the other
/// is a promise the seam does not keep.
int32_t af_machine_seam_triggered(const af_machine* box, uint32_t index);

/// Whether seam `index` has a pull outstanding — asked for, and its
/// point not reached since. Non-zero means yes.
///
/// The state that turns "the button did nothing" into "the button is
/// armed and the program has not been there yet". A seam whose point is
/// reached once a round is one a person can pull and watch do nothing
/// for a second, and #131's lesson is that the failure with nothing to
/// show is the one that reads as success.
int32_t af_machine_seam_waiting(const af_machine* box, uint32_t index);

/// How many times one of seam `index`'s armed points was **reached** —
/// the address matched at a step boundary — whether or not a handler ran
/// there (`machine::seam_status::reached`).
///
/// For an ordinary seam this is `af_machine_seam_fired`. For a triggered
/// one the difference is the measurement nobody has: `reached - fired` is
/// the arrivals nobody had asked for, and the rate of `reached` over a
/// run is the granularity at which a pull can possibly be served. A
/// `double` for the reason `_fired` is one — it is a 64-bit count.
double af_machine_seam_reached(const af_machine* box, uint32_t index);

/// How long the last served pull waited, in ticks: from the pull to the
/// arrival that ran the handler. Zero until one has been served. The
/// latency of the trigger, in the units this ABI states time in.
double af_machine_seam_waited(const af_machine* box, uint32_t index);

/// The tick the outstanding pull was made at. Meaningful only while
/// `af_machine_seam_waiting` is non-zero; a page showing how long a pull
/// has been waiting subtracts it from `af_machine_time`.
double af_machine_seam_pulled_at(const af_machine* box, uint32_t index);

/// Pull the trigger of the seam called `id`. `AF_OK` if the latch took;
/// `AF_INVALID` if there is no such seam, if it does not take a trigger,
/// or if it is off — `af_machine_seam_reason` does not carry that answer
/// (the refusal is about the pull and not about the seam's state), so a
/// caller that wants to explain it reads `_triggered` and `_state`.
///
/// A second pull while one is outstanding is `AF_OK` and changes
/// nothing: the latch is one-shot and is already set.
uint32_t af_machine_seam_pull(af_machine* box, const char* id);

/// Copy `size` bytes into the machine's memory at physical `address`.
///
/// The machine writing its own memory, not the program writing it: this
/// goes through the same back door a loader and the BIOS self-test use
/// (memory_map::ram), so it is not routed to a device, not refused by the
/// ROM region, and not reported as a touch of nothing.
///
/// It is how a self-written test program gets placed until the MZ loader
/// exists — the dev page (#55) and the CI smoke check both use it — and
/// it stays afterwards, because it is also how a host inspects and
/// arranges a machine.
///
/// `AF_INVALID` if the range does not fit inside the megabyte.
uint32_t af_machine_write_memory(af_machine* box, uint32_t address,
                                 const uint8_t* bytes, uint32_t size);

/// Copy `size` bytes of the machine's memory out to the caller's buffer,
/// from physical `address`. The read half of the above, and what lets a
/// smoke test hash a region the program wrote.
uint32_t af_machine_read_memory(af_machine* box, uint32_t address, uint8_t* out,
                                uint32_t size);

/// Set the entry state: CS:IP to start at, SS:SP to start with.
///
/// The four registers a loader sets. Everything else is already at its
/// power-on value, and a program placed with `af_machine_write_memory`
/// needs exactly these four to be runnable.
uint32_t af_machine_set_entry(af_machine* box, uint32_t cs, uint32_t ip,
                              uint32_t ss, uint32_t sp);

// --- Replay (machine/replay.h, docs/replay.md) ------------------------
//
// The two questions a host asks about a run it means to reproduce: what
// state is this machine in, and is it the run this recording describes.
//
// Recording is not here. A recording is written by the loop that made
// the run — where a key is posted, where a frame ends — and a host that
// has such a loop has `format_replay_line()`. Verifying is here because
// it is the opposite: no window, no speaker, no input of its own, and
// three targets that must answer it identically or "verified everywhere"
// means three readings of one file.

/// The whole-state hash right now, as 64 lowercase hex characters
/// written NUL-terminated into `out`; answers the length, or zero if
/// `out` is smaller than 65 bytes.
///
/// The same digest a recording's checkpoint carries (machine/state.h), so
/// a host can take one at any moment and compare it against a golden by
/// eye. What is in it and what is deliberately not is docs/replay.md §2.
uint32_t af_machine_state_hash(const af_machine* box, char* out, uint32_t max);

/// Run `box` through the recording in `text` and say whether it was that
/// run. `length` is the text's length in bytes; it need not be
/// NUL-terminated.
///
/// The recording's speed and seams are applied before they are checked —
/// a replay is the run the recording names — and then every initial
/// condition is checked against the machine and the filesystem, every key
/// delivered at the tick it was recorded at, and every checkpoint's hash
/// compared. The machine must be freshly reset with the program loaded
/// and nothing else done to it; this drives it to the recording's end.
///
/// Answers `AF_OK` when everything held and the end was reached, and
/// `AF_INVALID` otherwise. Either way `out` receives the one-line report
/// — what was verified, or what differed first and where — NUL-terminated
/// if it fits, and 512 bytes is enough for any of them.
uint32_t af_machine_verify_recording(af_machine* box, const char* text,
                                     uint32_t length, char* out, uint32_t max);

#ifdef __cplusplus
}  // extern "C"
#endif

// No casts in these: the operands are already uint32_t, and the core is
// built with -Wold-style-cast, which a C-compatible header cannot satisfy
// with a cast in it.
#define AF_VERSION_MAJOR(v) (((v) >> 16) & 0xFFu)
#define AF_VERSION_MINOR(v) (((v) >> 8) & 0xFFu)
#define AF_VERSION_PATCH(v) ((v) & 0xFFu)

#endif  // AMBERFOLIO_ABI_H
