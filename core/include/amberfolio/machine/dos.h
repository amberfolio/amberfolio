// SPDX-License-Identifier: AGPL-3.0-only
//
// The INT 21h subset (M2-D7, #52): PLAN.md §3's DOS surface, exactly and
// only — file open/create/read/write/seek/close/unlink/mkdir, date/time,
// console output, exit — as native handlers installed on the M2-F3
// callout (service_floor.h) over the M2-D5 filesystem (vfs.h).
//
// This file is composition, not new machinery: the mechanism that lets a
// native function answer an interrupt is service_floor.h's, the
// filesystem semantics and error codes are vfs.h's, the console byte sink
// and the wall clock are platform.h's. What is new here is the DOS
// handle table and the AH dispatch that ties them together.
//
//
// Where the state lives
// ----------------------
//
// A `service_handler` is a plain function pointer — `void(*)(service_floor&,
// std::uint8_t)`, nothing captured, nothing allocated (service_floor.h) —
// so the handle table cannot live in a closure. It lives on `machine`
// instead, as `dos_services`, reached from a handler body through
// `floor.box().dos()` exactly the way the timer handler reaches the BDA
// through `floor.box().processor()`. That makes `dos_services` a sibling
// of `console_output`/`wall_clock`/`input_queue`: state the machine owns
// for the whole of a run, present whether or not a program ever calls
// INT 21h, for the same reason those are.
//
// `install_dos_services()` is a separate call, not something the machine
// constructor makes — machine.h's own top comment says why: "INT 21h is
// M2-D7... a handler installed into the floor rather than a change to
// [machine.h]." A caller (a test today; M3's boot sequence later) calls
// it once the machine has a filesystem attached.
//
// The filesystem itself is `machine::vfs()` — a host- or test-attached
// pointer, nullable, the same shape `machine`'s `diagnostics*` already
// is. platform.h reserved the VFS as "the fifth thing" the platform
// interface lists and left where the pointer lives to whoever needed it
// first (M2-D6 the loader, or M2-D7 here); this is that decision, made
// once so the loader does not have to make a second one.
//
//
// The handle table
// -----------------
//
// DOS's own per-program table of small integers, distinct from
// `vfs.h`'s `file_handle` (a backend's own bookkeeping) and from
// `memory_filesystem::max_open_handles` (that backend's concurrency
// bound) — see vfs.h's top comment for why those two are deliberately
// not this one. Twenty slots, the classic DOS `CONFIG.SYS FILES=`
// default (`memory_vfs.h` cites the same fact for its own, lower-level
// bound), pre-loaded with the five standard handles DOS boots with:
//
//   * **0, 3, 4** (STDIN, STDAUX, STDPRN) are the **documented sink**
//     the issue's scope calls for. Nothing backs any of them in M2 — no
//     keyboard service (#53), no serial or parallel device — so a write
//     reports every byte accepted and drops them, and a read answers
//     zero bytes, the same "nothing here" answer end-of-file already
//     gives. That is the honest M2 answer (PLAN.md §3's "log, don't
//     fake" applied to a device that plain does not exist yet), not a
//     guess at what #53 or a printer device will eventually do.
//   * **1, 2** (STDOUT, STDERR) are the **console sink**: a write goes
//     to `console_output`. There is no console input path at all (no
//     byte stream exists for it — platform.h's console section is
//     output only), so a read from either answers zero bytes exactly
//     like the documented sink does.
//
// Because 0-4 are always open, the first `create()`/`open()` a program
// makes allocates slot 5 — "exactly as programs written against DOS
// expect," the issue's own words, and worth stating because it is a
// program-observable fact, not an implementation detail: code of the
// era hard-codes handle 5 for its first data file often enough that
// getting this wrong would be a silent compatibility bug.
//
//
// The size-0 write and why it needed a new VFS primitive
// ---------------------------------------------------------
//
// DOS's own convention: AH=40h with CX=0000h writes nothing and instead
// sets the file's length to the handle's current position — shrinking it
// if the position is behind the old end, extending it with zero fill if
// the position is ahead (documented DOS behaviour, e.g. Ralf Brown's
// Interrupt List at AH=40h; a fact about the API, not an artifact).
// `filesystem::write()` cannot produce a shrink — every path through it
// only grows `length` — so this is the one place #52 widens vfs.h's
// interface rather than only composing over it: `filesystem::truncate()`
// is the missing primitive, implemented in `memory_vfs.cpp` by the same
// gap-fill rule `write()` already documents for a seek past the old end
// of file.
//
//
// Access-mode and origin validation that VFS never sees
// ---------------------------------------------------------
//
// AH=3Dh's AL and AH=42h's AL are validated here, before anything reaches
// `filesystem`: an access mode outside 0-2 or a seek origin outside 0-2
// is not a filesystem condition at all (canonicalize()'d nothing, opened
// nothing), so it is reported with DOS's own raw error numbers
// (`invalid_function_code`, `invalid_access_code` below) rather than
// forced through `vfs_error`, which has no entries for either — inventing
// one to reuse `dos_error_code()` would be pretending a VFS-level cause
// for a DOS-level mistake.
//
//
// Ctrl-Break, the DOS half
// -------------------------
//
// `break_intercepted()` is the whole of it: read `bda::break_flag`, and
// if it is set, clear it and deliver INT 23h — the same real interrupt
// delivery the timer handler uses for the user tick (service_floor.cpp)
// — through a **continuation stub** (service_floor.h), exactly the
// mechanism the timer's own second half uses and for the same reason: a
// handler that has to let the emulated machine run (the program's INT
// 23h routine) and then carry on needs somewhere of its own to resume,
// because `machine::dispatch_services()` cannot otherwise tell "a fresh
// call reached the INT 21h stub" from "a nested interrupt just returned
// to it" — both look like "CS is the stub segment" at the next boundary,
// and the former re-dispatches AH all over again.
//
// So the detour resumes at a claimed continuation, not at the INT 21h
// stub's own address: `deliver_interrupt` pushes that continuation as
// the return address, the program's INT 23h handler (or, unhooked, this
// machine's honest refusal of a vector nothing backs — see below)
// eventually IRETs onto it, the continuation's own body does nothing at
// all, and *its* stub's IRET is what actually executes next — popping
// not the continuation's frame but the frame still underneath it, the
// **original** INT 21h call's, because delivering INT 23h never touched
// that one. The console-affecting function that was about to run simply
// never does; control lands back with the program exactly where the
// original INT 21h calls always return to. That is what real DOS's break
// check does before performing the I/O, achieved here with no state of
// its own to keep straight — the continuation's body is empty, the same
// "the trace shows the call, there is nothing else to do" reasoning
// `nothing_to_do()` already uses for the unhooked user tick vector.
//
// Vector 0x23 is deliberately left unbacked by #52: if the flag is ever
// set (nothing sets it until #53 lands) and the program has not hooked
// its own handler, the machine stops exactly as any other unbacked
// vector does. Inventing a default "abort to the command line" would be
// inventing what a DOS-less machine's command line even is; the honest
// answer is the same loud refusal PLAN.md §3 asks for everywhere else.
//
// Checked on the three console-affecting functions the scope lists
// (AH=02h, AH=09h, and AH=40h when the handle is console-backed) and
// nowhere else — "keep it minimal," the issue's own words.

#pragma once

#include <array>
#include <cstdint>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

class machine;
class service_floor;
class state_sink;

/// DOS's own function-number-invalid error (0x01) and invalid-access-code
/// error (0x0C) — the DOS 2.x extended error table's entries for the two
/// validation failures this layer must report without ever reaching
/// `filesystem` (see this file's top comment). Named and placed beside
/// `vfs.h::dos_error_code()` in spirit, not merged into it: these are not
/// `vfs_error` values, because nothing about them is the VFS's to decide.
inline constexpr std::uint16_t invalid_function_code = 0x01;
inline constexpr std::uint16_t invalid_access_code = 0x0C;

/// The one drive this machine has, as DOS's own zero-based drive number:
/// C is 2. vfs.h's top comment is where "there is one drive, C:" is
/// decided (PLAN.md §3); this is that decision expressed in the units
/// AH=19h and AH=44h answer in.
inline constexpr std::uint8_t only_drive = 2;

/// The DOS-level per-program handle table and exit state — everything an
/// INT 21h handler needs beyond the filesystem and the platform interface
/// `machine` already carries. See this file's top comment for why it
/// lives here rather than in a closure.
class dos_services {
 public:
  /// Slots in the table, standard handles included. The classic DOS
  /// `CONFIG.SYS FILES=` default — `memory_vfs.h` cites the same fact for
  /// its own, lower-level open-handle bound.
  static constexpr std::uint16_t max_handles = 20;

  /// `open_file()`'s answer when the table has no room — DOS's own
  /// too-many-open-files condition, one level above the backend's.
  static constexpr std::uint16_t no_handle = 0xFFFF;

  /// What a DOS handle is backed by. See this file's top comment for what
  /// each answers to read and write.
  enum class handle_kind : std::uint8_t {
    /// A real, VFS-backed open file — `handle_state::backing` names it.
    file,
    /// Handles 1 and 2: writes reach `console_output`; there is no
    /// console input path (platform.h's console section is output only),
    /// so reads answer zero bytes.
    console,
    /// Handles 0, 3 and 4: the documented sink. Writes report every byte
    /// accepted and discard them; reads answer zero bytes.
    null_sink,
  };

  struct handle_state {
    bool in_use{};
    handle_kind kind{};
    /// Meaningful only when `kind == handle_kind::file`.
    file_handle backing{};
    /// The canonical path the handle was opened on, for `file` handles.
    /// Kept so a read can be recorded by name (overlay.h, M4-F3 #97):
    /// the overlay tracker identifies a module by the file it came from,
    /// and the backend's `file_handle` says nothing about that.
    dos_path path{};
    /// Whether anything has been written through this handle since it
    /// was opened — AH=44h AL=00h's bit 6 for a file handle, which DOS
    /// reports *clear* once a write has happened. Tracked rather than
    /// guessed: it is one bool per slot and the alternative is picking a
    /// value and hoping no program reads it.
    bool written{};
  };

  /// Whether any bytes have been **read** through a handle since it was
  /// opened.
  ///
  /// **Not machine state**, on `overlay.h`'s own terms and for its own
  /// reason: no DOS call reports it, so no program can observe it; a
  /// `reset()` drops it; `save_state()` below never writes it; and a
  /// replay reconstructs it, because the same run makes the same reads.
  /// It is deliberately *not* a field of `handle_state`, every one of
  /// which is serialized — a flag that is not machine state must not sit
  /// where the next person to add a field will serialize it.
  ///
  /// It is here because a **close** is the only moment at which "this
  /// file was actually read" can be said, and the difference between a
  /// program reading a save game and a program merely asking whether one
  /// exists is invisible without it. The automap's exploration sidecar
  /// (#173) is what has to tell those apart; the proven design's own file
  /// layer told them apart the same way, by whether bytes moved.
  [[nodiscard]] bool read_through(std::uint16_t handle) const noexcept;

  dos_services() noexcept { reset(); }

  /// Back to power-on: every handle closed, then the five standard ones
  /// reopened, and the exit state cleared. `machine::reset()` calls this
  /// so a warm boot starts the next program with the same table a cold
  /// one gives the first — the handle table is a running program's state,
  /// not wiring, unlike the handlers `install_dos_services()` installs
  /// (service_floor.h makes the same distinction for its own handlers).
  void reset() noexcept;

  /// Claim a slot for an already-open VFS `file`, opened on `path`.
  /// `no_handle` if the table is full — the caller (AH=3Ch/3Dh) must
  /// close `file` on the backend in that case, or a file the program can
  /// never reach stays open until the run ends.
  [[nodiscard]] std::uint16_t open_file(file_handle file,
                                        const dos_path& path) noexcept;

  /// Release `handle`. False if it was not open — the caller reports
  /// `vfs_error::invalid_handle`.
  bool close(std::uint16_t handle) noexcept;

  /// Record that a write went through `handle` — see `handle_state`'s
  /// `written`. Silently ignores a handle that names nothing open,
  /// because the caller has already reported that.
  void note_written(std::uint16_t handle) noexcept;

  /// The same, for bytes read. See `read_through()` for what it is and
  /// what it deliberately is not.
  void note_read(std::uint16_t handle) noexcept;

  /// The state behind `handle`, or null if it names nothing open.
  [[nodiscard]] const handle_state* find(std::uint16_t handle) const noexcept;

  /// The handle table and the exit state (state.h). The position of each
  /// open file is the backend's and is asked of it by `machine`, which
  /// has the filesystem; this writes what the table itself holds.
  void save_state(state_sink& out) const;

  // --- Exit state -------------------------------------------------------
  //
  // AH=4Ch and INT 20h both end here (this file's top comment); #51's
  // loader is what a host or a test reads the code back through, via
  // `machine::stop().exit_code` under `stop_reason::program_exited`
  // (#51 owns that seam; this issue calls into it).

  void record_exit(std::uint8_t code) noexcept {
    exit_code_ = code;
    exited_ = true;
  }

  [[nodiscard]] bool exited() const noexcept { return exited_; }
  [[nodiscard]] std::uint8_t exit_code() const noexcept { return exit_code_; }

 private:
  std::array<handle_state, max_handles> handles_{};

  /// `read_through()`'s flags — a parallel array rather than a field of
  /// `handle_state`, because that struct is serialized in full and this
  /// is not machine state. See `read_through()` above.
  std::array<bool, max_handles> read_through_{};
  std::uint8_t exit_code_{};
  bool exited_{};
};

/// Install the INT 21h dispatcher and the INT 20h terminator onto
/// `floor`. Call once a machine has a filesystem attached
/// (`machine::set_filesystem()`) — every file function reads it through
/// `floor.box().vfs()` at call time, not at install time, so the order
/// only matters by the time a program actually opens a file.
void install_dos_services(service_floor& floor);

}  // namespace amberfolio::machine
