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

/// A document was read fine and is not one this build knows
/// (`af_machine_present_document`, M5-D3 #171).
///
/// Its own code, and not `AF_INVALID`, for the reason `AF_NO_ROOM` has
/// its own: nothing about the request was wrong. PLAN.md §9 names
/// edition variance as a real risk — players hold re-scanned PDFs and
/// releases nobody here has seen — and the mitigation it asks for is a
/// friendly message and a process for adding editions. A host cannot
/// give a player either one if "this is not a PDF I can read" and "this
/// is a document I do not recognize" arrive as the same number.
#define AF_UNRECOGNIZED 7u

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

/// A buffer that is always big enough for any path this machine can name
/// (`af_machine_vfs_path_at`), terminator included — eight components of
/// `FILENAME.EXT`, a separator before each, and one more for the root.
///
/// A number rather than a call, because a C caller wants an array size
/// and a JS one wants a `_malloc` argument, and both want it before they
/// have a machine. `machine::dos_path_capacity` is the same number in
/// core, and abi.cpp static_asserts that they have not drifted apart —
/// which is what stops this from being a second answer to a question core
/// already answers.
#define AF_PATH_CAPACITY 106u

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

// --- The ABI's own version --------------------------------------------

/// The version of *this contract*, which is not the version of the core.
///
/// `af_version` answers "which build is this" — the milestone's number,
/// tied to `project(VERSION ...)` and to the tag. It moves whenever
/// anything in the repository moves, which makes it useless for the one
/// question a consumer has to answer before it loads a module: **do I
/// speak what this bundle exports?** A site that pins a release and
/// refuses to build on a hash mismatch (#200) still has to decide, at
/// load time and before instantiating anything, whether the entry points
/// its loader calls are the entry points that are there.
///
/// So the two numbers are separate, and this pair is the one carrying the
/// compatibility rule:
///
///   * **major** moves when an entry point that existed is removed or
///     renamed, or when what one does — or what its arguments mean —
///     changes. A host written against major N may not assume it can
///     drive major N+1.
///   * **minor** moves when entry points are added and nothing that was
///     already there changed. A host written against 1.0 still drives
///     1.3, and merely does not know what 1.3 grew.
///
/// **The surface this covers is the module's, not this header's.** The
/// wasm build exports what `hosts/web/CMakeLists.txt` lists, which is this
/// header's functions *plus* that host's own `af_web_*` ones, and a
/// consumer calls both kinds without being able to tell them apart. Both
/// are in the release manifest's `exports`, and a change to either moves
/// these numbers. A function added *here* and not added *there* is simply
/// absent from the module and moves nothing — the trap the top of this
/// header states, and what the smoke check exists to catch.
///
/// **1.0 is the first declared ABI, and claims nothing retroactively.**
/// Releases staged before it carry no `abi` in their manifest, and a
/// consumer reads that absence as "older than the declaration" rather
/// than as 1.0. The numbers are the ABI's alone: 1.0 does not say the
/// project reached 1.0, and PLAN.md's own 1.0 is a different milestone
/// that will not move them by arriving.
///
/// Read by `scripts/release-bundle.sh`, which puts them in
/// `manifest.json` so that decision can be made from a file that costs a
/// fetch rather than from a module that costs an instantiation. Nothing
/// in core reads them.
#define AF_ABI_VERSION_MAJOR 1u
#define AF_ABI_VERSION_MINOR 0u

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

// --- The edge log: what was published, in words (M4-A1, #106; #148) ---
//
// `af_machine_render_audio` above answers "what does it sound like".
// This answers the other half, which is a different question with a
// different owner: *which* edges did the machine publish, at *which*
// ticks. A wrong-sounding run is either a machine that made the wrong
// edges or a filter that rendered the right ones badly, and until these
// three the second was the only one a browser could look at.
//
// The SDL host's `--dump PREFIX` has written exactly this as
// `PREFIX.edges` since #106. #148 item 5 is that a browser could not ask
// it at all, so the half of #106 that is a measurement rather than an
// ear stopped at the desktop.
//
// Off unless asked for, drained rather than accumulated, and **not
// machine state**: `af_machine_reset` leaves the setting alone,
// `af_machine_state_hash` cannot see any of it, and a recording made
// with the log on verifies against one made with it off. An observation
// of a run is not part of the run (platform.h says this at length).

/// Start or stop logging edges. Survives `af_machine_reset`; what the
/// log already holds does not.
void af_machine_audio_log_edges(af_machine* box, int32_t on);

/// Non-zero if the log is on.
int32_t af_machine_audio_logging_edges(const af_machine* box);

/// Drain up to `max` logged edges, oldest first, into the caller's two
/// arrays: `at[i]` is the tick the output changed, `level[i]` is 0 or 1
/// for what it changed to. Answers how many were written, and removes
/// exactly those from the log. Zero for a null handle or a null array.
///
/// Two arrays rather than one of pairs: a JS host reads each through one
/// typed-array view, and an array of structs would make it know this
/// ABI's alignment. A `double` for a tick for the reason
/// `af_machine_time` is one.
///
/// Machine-thread only. `af_machine_render_audio` is the one call in
/// this file that may be made from elsewhere, and this is not it — both
/// ends of the log are the producer's side (platform.h).
uint32_t af_machine_audio_read_edges(af_machine* box, double* at,
                                     uint8_t* level, uint32_t max);

/// How many edges the log had no room for, over the run. A host that
/// drains every frame never sees this move; one that does not needs it
/// to say "this list has a hole in it" rather than presenting a short
/// list as a whole one.
///
/// Distinct from a *ring* overflow, which is sound the host never got to
/// hear. A drop here costs an observation and nothing else.
double af_machine_audio_edges_dropped(const af_machine* box);

/// Edges the log is holding right now, waiting to be drained.
double af_machine_audio_edges_pending(const af_machine* box);

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

/// How many **files** the filesystem holds, across the whole tree, and
/// the name, path and size of one of them. The order is a depth-first
/// walk from the root taking each directory's entries in the VFS's own
/// pinned name order (machine/vfs.h), so a listing is the same on every
/// host and in every run.
///
/// **The whole tree, since M5-D2 (#170), and it was the root alone.**
/// `af_machine_vfs_put` has reached below the root since #146 and the
/// listing never followed it, so a browser could hand over an
/// installation and not read back a single thing under `\SAVE\` — the
/// same gap #146 closed on the way in, still open on the way out. The
/// exploration sidecar (#173) and M6's persistence both walk out through
/// here, and neither of them lives at the root.
///
/// **Files, and only files.** What this lists is exactly the set
/// `af_machine_vfs_get` can read and `af_machine_vfs_remove` can take
/// away, which is what lets a row be a path and a size with no kind flag
/// beside it. The honest consequence is that an *empty* directory is
/// invisible here: it is a name with no contents, every directory that
/// matters is implied by the path of a file inside it, and nothing a
/// caller can do with this listing could be done to a directory anyway.
///
/// **The listing is taken by `af_machine_vfs_count`**, and the three
/// `_at` calls read the rows it took. A caller lists by asking for the
/// count and then reading the rows, which is what every caller does; a
/// row read without a preceding count, or after the filesystem has
/// changed, is the listing as it was, and asking for the count again
/// takes a new one.
///
/// That is a contract and not an implementation detail leaking out.
/// `entry_at()` is a selection over the backend's unsorted table, so
/// enumerating a directory of *n* through it is already quadratic
/// (machine/memory_vfs.h says why that backend keeps no sorted index),
/// and a tree walk per row would square that again — a real
/// installation's hundred and ninety-odd files did not finish inside the
/// wasm smoke check's two-minute timeout that way. One walk per listing
/// is the only shape that works. It is also the more honest listing: a
/// live one shifted its rows underneath a caller's loop whenever
/// anything changed the filesystem in between, and said nothing.
///
/// `af_machine_vfs_name_at` writes the file's **leaf name** into `out`,
/// NUL-terminated, and answers its length — zero for an index past the
/// end or a buffer smaller than 13 bytes (`FILENAME.EXT` and its
/// terminator).
///
/// `af_machine_vfs_path_at` writes the whole path, `\SAVE\SAVE1.DAT`,
/// separators and leading separator included — the spelling
/// `machine::format_dos_path` gives it, so a path in a listing and a path
/// in a log line are the same characters. It answers its length, or zero
/// for an index past the end or a buffer that will not hold it;
/// `AF_PATH_CAPACITY` below is always enough. It is what tells
/// `SAVE1.DAT` at the root from `SAVE\SAVE1.DAT` one directory down, and
/// it is what the other two calls in this section take.
/// **This listing and a recording's manifest are not the same list, and
/// should not be read as one.** A manifest (machine/replay.h) names every
/// *entry* of the disk a run started from — directories included, empty
/// ones especially, because an empty directory is a fact about a disk
/// that nothing else records — and spells each one relative to the root,
/// with no leading separator (`SAVE\\CHARLIST.TXT`). This names every
/// *file* that is there now, absolutely, the way a program would ask DOS
/// for it (`\\SAVE\\SAVE1.DAT`). Both are `\\`-joined and both are in the
/// same pinned depth-first order, so a reader comparing them is
/// comparing components rather than conventions; the two differ in what
/// they are *for*, which is why neither was made to look like the other.
///
uint32_t af_machine_vfs_count(const af_machine* box);
uint32_t af_machine_vfs_name_at(const af_machine* box, uint32_t index,
                                char* out, uint32_t max);
uint32_t af_machine_vfs_path_at(const af_machine* box, uint32_t index,
                                char* out, uint32_t max);
uint32_t af_machine_vfs_size_at(const af_machine* box, uint32_t index);

/// How many bytes the file at `path` holds. Zero for a path that names
/// no file — and zero for a file of no bytes, which
/// `af_machine_vfs_get(box, path, 0, 0)` tells apart: it answers `AF_OK`
/// for the one and `AF_INVALID` for the other.
///
/// The size-query half of the query-then-fill pair. `path` is raw text,
/// canonicalized here like every other one in this section.
uint32_t af_machine_vfs_size(const af_machine* box, const char* path);

/// Copy the whole of `path` into `out`, which must be at least the size
/// `af_machine_vfs_size` answered.
///
/// `AF_OK` when every byte of the file is in `out`. `AF_NO_ROOM`, and
/// nothing copied, when `max` is smaller than the file — the caller's
/// buffer, not the machine's storage, and the same code for the same
/// reason: nothing about the request was wrong, there was nowhere to put
/// the answer. `AF_INVALID` for a path that resolves to nothing, to a
/// directory, or to the root, and for a `path` no DOS path can equal.
/// `AF_NO_FILESYSTEM` when there is no filesystem attached.
///
/// `out` may be null when `max` is zero, and then this is an existence
/// test that works on a file of no bytes.
///
/// The read half of the door #170 opened. A browser could hand an
/// installation over one file at a time and never read one byte back;
/// what wants to is a page persisting what the program wrote — the
/// exploration sidecar now (#173), IndexedDB next (M6).
uint32_t af_machine_vfs_get(af_machine* box, const char* path, uint8_t* out,
                            uint32_t max);

/// Delete the file at `path`.
///
/// `AF_OK` when it is gone. `AF_INVALID` for a path that names no file,
/// for one that names a **directory** or the root, and for one no DOS
/// path can equal. `AF_NO_FILESYSTEM` when there is no filesystem
/// attached.
///
/// **A file, and only a file, and the directory above it stays.** That is
/// the decision #170 asked for, and it is core's shape rather than a
/// choice made here: `machine::filesystem` has no `rmdir`, deliberately,
/// because nothing in PLAN.md §3's INT 21h subset removes a directory
/// (AH=3Ah is not in it). Inventing one at this boundary would put a
/// second path-removal rule above the interface that owns path
/// semantics, which is the thing #146 settled must not happen. An empty
/// directory left behind is a name with nothing in it: invisible to
/// `af_machine_vfs_count`, harmless to the next `af_machine_vfs_put`
/// under it, and gone with `af_machine_vfs_clear`.
///
/// **On the desktop this is a real file on a real disk.** The ABI only
/// ever reaches the in-memory backend, but the same operation over
/// `directory_vfs` — which the SDL host's `--vfs-remove` is — deletes the
/// player's file. The host says so before it does it.
uint32_t af_machine_vfs_remove(af_machine* box, const char* path);

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

/// What seam `index`'s row *means* — did it act, and if not why not — as
/// the one sentence `machine::seam_reading_text` gives it, ready to
/// append to a printed row (leading " - " and all), NUL-terminated into
/// `out`. Empty when the numbers say everything there is to say.
/// Answers the length, or zero for a bad index or a small buffer.
///
/// Text and not an enumerator on purpose (#163). The desktop host and
/// the page each used to decide this for themselves out of the numbers
/// beside it, and each got it wrong the same way the moment a seam could
/// act somewhere other than at an address: both printed "armed and never
/// reached" over a run in which the seam had done what it was asked.
/// Handing over the finished sentence is what makes a browser row and a
/// terminal row incapable of disagreeing, rather than merely expected
/// not to.
uint32_t af_machine_seam_reading(const af_machine* box, uint32_t index,
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

// --- Host services (M5-D1, #169) ---------------------------------------
//
// The seam -> host direction, seen from the page. A seam may call out to
// a host service (machine/seam.h): the journal reader, or the automap.
// The implementation is C++ inside the module on both targets
// (hosts/common), because it is handed the machine and what it reads
// there is only true at the moment of the call — so what crosses *this*
// boundary is not the call but the record of it.
//
// **Polled, and paired with whatever a host does with the call.** #153 is
// the lesson: a stream cannot express "it never asked". A page watching
// only for something to happen cannot tell a callout that was served
// from one that was never made, because both look like nothing — and
// "the callout never happened" is exactly the failure a new door has. So
// these two sit beside the seam's own event line the way
// `af_machine_seam_fired` sits beside the `seam_event` stream.
//
// `which` is a `machine::seam_host_service`: 0 for the journal reader's
// `journal_open`, 1 for `automap_update`. There were three until #169;
// `save_state_changed` went with the enhancement that would have called
// it (#176, withdrawn), because a service with no consumer is a surface
// built on spec.

/// How many calls of `which` a host has **served** on this machine since
/// the last reset. Zero for a `which` that is not a service.
///
/// Served, not asked: a call made on a machine with no host attached
/// does not count, because nothing happened. A non-zero answer is
/// therefore proof that an implementation was reached — which is the
/// question this call exists to answer.
///
/// A `double` for the reason `af_machine_seam_fired` is one: it is a
/// count that can outgrow a 32-bit integer over a long run, and every
/// number the JS side reads is a double anyway.
double af_machine_seam_host_calls(const af_machine* box, uint32_t which);

/// What the most recent served call of `which` carried, or zero if there
/// has not been one. The count says a journal entry was asked for; this
/// says which entry.
uint32_t af_machine_seam_host_argument(const af_machine* box, uint32_t which);

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

// --- Document gates (M5-D3, #171) --------------------------------------
//
// PLAN.md §5 gates two enhancements on a document the player holds — the
// code-wheel bypass on the code wheel, the journal on the journal — and
// the rule is exact: "a possession gate: it demonstrates the player holds
// the document, no more". So what crosses this boundary is bytes, once,
// and what comes back is a fingerprint and a name.
//
// Nothing here reads *inside* a document. A gate is over bytes
// (machine/document.h); the journal's extractor (#174) reads inside its
// own document and is separate work with its own issue.
//
// A gate is one more condition on whether a seam arms, in the same place
// "is the module resident" is: a seam whose gate is unsatisfied is on and
// **inert**, with `af_machine_seam_reason` answering
// `document_not_presented`. It is not refused — the seam took, and the
// player has not shown the thing PLAN.md §5 requires them to hold.
//
// Presenting is **configuration**, like a seam toggle: a host does it
// between `af_machine_run_until` slices and never from inside one, it is
// not machine state, it is not in the state hash, and a machine with a
// document presented and every seam off is byte-for-byte the machine
// without one.

/// The player presented a document: `size` bytes at `bytes`.
///
/// `out` receives the document's SHA-256 as 64 lowercase hex characters,
/// NUL-terminated, whatever the answer — including for one this build
/// does not know, which is the whole point of writing it either way. A
/// player holding an unrecognized edition can be shown the fingerprint
/// of the file they hold, and that is what turns "this does not work"
/// into a line somebody can add to the table.
///
/// `AF_OK` when it is an edition this build knows, and then every gate
/// of that document's kind is satisfied for the life of this machine.
/// `AF_UNRECOGNIZED` when the bytes hashed fine and name no edition —
/// nothing is satisfied, and nothing is guessed. `AF_INVALID` for a null
/// pointer, no bytes, or an `out` smaller than 65 bytes.
///
/// The bytes are hashed and dropped. This machine never keeps a document
/// (PLAN.md §2, §6).
uint32_t af_machine_present_document(af_machine* box, const uint8_t* bytes,
                                     uint32_t size, char* out, uint32_t max);

/// How many documents have been presented and recognized, and the name
/// of one of them — what a host prints back so a run says what was shown
/// to it.
///
/// `af_machine_document_name_at` writes the name NUL-terminated into
/// `out` and answers its length, or zero for an index past the end or a
/// buffer too small.
uint32_t af_machine_document_count(const af_machine* box);
uint32_t af_machine_document_name_at(const af_machine* box, uint32_t index,
                                     char* out, uint32_t max);

/// What document seam `index` is gated on, as a name a host shows
/// (`machine::document_kind_name`) — `code wheel`, `journal`, or `no
/// document` for a seam with no gate, which is every seam in this build
/// today. Written NUL-terminated into `out`; answers its length.
uint32_t af_machine_seam_gate(const af_machine* box, uint32_t index, char* out,
                              uint32_t max);

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
