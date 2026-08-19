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
#define AF_UNIMPLEMENTED 4u

// There is no "a machine already exists" code, because no function can
// return one: `af_machine_create` answers a pointer, and null is the
// whole of what it has to say. A status nothing produces is a status a
// host would write a branch for and never reach.

// --- Speed presets ----------------------------------------------------
//
// The values of machine::speed_preset, restated for a C caller. Asserted
// against it in abi.cpp, so the two cannot drift apart.

#define AF_SPEED_PC_XT 0u
#define AF_SPEED_TURBO_XT 1u
#define AF_SPEED_AT 2u

// --- Key actions ------------------------------------------------------

#define AF_KEY_UP 0
#define AF_KEY_DOWN 1

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

/// Non-zero once the machine has stopped. Sticky until reset.
int32_t af_machine_stopped(const af_machine* box);

/// Why it stopped: the value of machine::stop_reason, with 0 meaning
/// "running". The detail behind it — the opcode, the service, the address
/// — goes to the diagnostics sink, which is a C++ interface and not part
/// of this ABI; a dev page shows the number and the console output.
uint32_t af_machine_stop_reason(const af_machine* box);

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

// --- Getting a program in ---------------------------------------------

/// Load an MZ executable: relocations, PSP, entry state, the lot.
///
/// **Reserved, and answers `AF_UNIMPLEMENTED` today.** The loader is
/// M2-D6 (#51) and the file it reads comes from the VFS, which is M2-D5
/// (#50). The entry point is here now so that the export list, the JS
/// host and this documentation are settled before the loader lands, and
/// so that a host calling it early gets a distinct, loud answer rather
/// than a machine that quietly does nothing.
uint32_t af_machine_load_program(af_machine* box, const uint8_t* image,
                                 uint32_t size);

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
