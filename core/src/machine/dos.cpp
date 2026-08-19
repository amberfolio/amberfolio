// SPDX-License-Identifier: AGPL-3.0-only
//
// The handlers dos.h's top comment describes. Read that first — this file
// is the AH dispatch and the register plumbing it promises, and every
// design decision (where the state lives, the size-0 truncate, the
// validation that never reaches vfs.h, Ctrl-Break) is argued there rather
// than repeated here.

#include "amberfolio/machine/dos.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

void dos_services::reset() noexcept {
  handles_ = {};

  // The five standard handles, always open, so the next create()/open()
  // is always handle 5 (dos.h's top comment).
  handles_[0] = {.in_use = true, .kind = handle_kind::null_sink};
  handles_[1] = {.in_use = true, .kind = handle_kind::console};
  handles_[2] = {.in_use = true, .kind = handle_kind::console};
  handles_[3] = {.in_use = true, .kind = handle_kind::null_sink};
  handles_[4] = {.in_use = true, .kind = handle_kind::null_sink};

  exit_code_ = 0;
  exited_ = false;
}

std::uint16_t dos_services::open_file(file_handle file) noexcept {
  for (std::uint16_t i = 0; i < max_handles; ++i) {
    if (!handles_[i].in_use) {
      handles_[i] = {
          .in_use = true, .kind = handle_kind::file, .backing = file};
      return i;
    }
  }
  return no_handle;
}

bool dos_services::close(std::uint16_t handle) noexcept {
  if (handle >= max_handles || !handles_[handle].in_use) {
    return false;
  }
  handles_[handle] = handle_state{};
  return true;
}

const dos_services::handle_state* dos_services::find(
    std::uint16_t handle) const noexcept {
  if (handle >= max_handles || !handles_[handle].in_use) {
    return nullptr;
  }
  return &handles_[handle];
}

namespace {

using cpu::reg16;
using cpu::reg8;
using cpu::sreg;

/// DOS's own Ctrl-Break handler vector. Not `cpu::interrupt_vector` —
/// that namespace is the processor's self-raised vectors (0-4); this one
/// belongs to DOS, the way 0x21 and 0x20 do.
constexpr std::uint8_t ctrl_break_vector = 0x23;

/// The continuation slot INT 23h's detour resumes at — see dos.h's top
/// comment for why it needs one at all. Slot 0 is the timer's
/// (service_floor.h); this is the next one, not shared with it.
constexpr unsigned break_continuation_slot = 1;

/// How many bytes an ASCIZ pathname read from the emulated machine may
/// run to before this layer gives up looking for the terminating NUL.
/// `dos_path` can hold at most `max_depth` (8) components of at most
/// `dos_name::max_length` (12) chars each plus separators — comfortably
/// under 128 even before `canonicalize()`'s own per-component validation
/// gets a chance to refuse anything that does not fit; there is no
/// legitimate path this ever truncates.
constexpr std::size_t max_raw_path = 128;

/// One I/O chunk: a classic DOS sector, and the size of the stack buffer
/// every file read/write/console-write below moves through. core/ has no
/// heap (PLAN.md §4), so a 64 KiB CX cannot be staged in one buffer; a
/// bounded chunk moved in a loop is the fixed-capacity house pattern
/// applied to a transfer instead of a table.
constexpr std::size_t io_chunk_size = 512;

[[nodiscard]] std::uint16_t low_half(std::uint32_t value) noexcept {
  return static_cast<std::uint16_t>(value & 0xFFFFu);
}

[[nodiscard]] std::uint16_t high_half(std::uint32_t value) noexcept {
  return static_cast<std::uint16_t>(value >> 16u);
}

[[nodiscard]] std::uint32_t pack32(std::uint16_t high,
                                   std::uint16_t low) noexcept {
  return (static_cast<std::uint32_t>(high) << 16u) | low;
}

[[nodiscard]] std::size_t chunk_len(std::uint16_t remaining) noexcept {
  return remaining < io_chunk_size ? std::size_t{remaining} : io_chunk_size;
}

// --- The DOS/AX error convention (service_floor.h, dos.h) --------------

void fail(service_floor& floor, std::uint16_t code) {
  floor.box().processor().regs()[reg16::ax] = code;
  floor.set_caller_carry(true);
}

void fail(service_floor& floor, vfs_error err) {
  fail(floor, dos_error_code(err));
}

void succeed(service_floor& floor) { floor.set_caller_carry(false); }

void succeed_with(service_floor& floor, std::uint16_t ax) {
  floor.box().processor().regs()[reg16::ax] = ax;
  floor.set_caller_carry(false);
}

/// The filesystem file functions need, or null with the machine already
/// stopped. See dos.h's top comment: a machine with no filesystem
/// attached at all is a wiring precondition, not a DOS-observable
/// condition, so it is refused the same loud way an unbacked function is
/// rather than forced through a `vfs_error` that does not describe it.
[[nodiscard]] filesystem* require_vfs(service_floor& floor) {
  filesystem* fs = floor.box().vfs();
  if (fs == nullptr) {
    floor.box().stop_unimplemented_function(
        cpu::physical_address(floor.caller_cs(), floor.caller_ip()));
  }
  return fs;
}

/// Read an ASCIZ pathname from DS:DX and canonicalize it against the
/// root. There is no chdir/getcwd in this subset (PLAN.md §3's DOS
/// surface does not list them), so the current directory is always the
/// root — when a later issue adds AH=3Bh/47h, this is the one call site
/// that starts reading real per-machine state instead.
[[nodiscard]] vfs_result<dos_path> read_path(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];

  std::array<char, max_raw_path> raw{};
  std::size_t length = 0;
  while (length < max_raw_path) {
    const std::uint8_t byte =
        cpu.read_byte(segment, static_cast<std::uint16_t>(offset + length));
    if (byte == 0) {
      break;
    }
    raw[length] = static_cast<char>(byte);
    ++length;
  }

  return canonicalize(dos_path{}, std::span<const char>(raw.data(), length));
}

// --- Ctrl-Break, the DOS half (dos.h's top comment) ---------------------

/// What runs when a console-affecting call's INT 23h detour returns.
/// Nothing: the detour's whole purpose is served by resuming *here*
/// rather than back on the INT 21h stub (dos.h's top comment), and this
/// stub's own IRET — the next thing `machine::step()` executes once this
/// returns "handled" — is what actually unwinds the original call. There
/// is no second half the way the timer's continuation has one.
void break_continuation_done(service_floor& /*floor*/,
                             std::uint8_t /*vector*/) {}

[[nodiscard]] bool break_intercepted(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint8_t flag = cpu.read_byte(bda::segment, bda::break_flag);
  if ((flag & bda::break_flag_bit) == 0) {
    return false;
  }

  cpu.write_byte(bda::segment, bda::break_flag,
                 static_cast<std::uint8_t>(flag & ~bda::break_flag_bit));

  // Resume at the continuation once INT 23h returns, not at this stub's
  // own address (dos.h's top comment: the step boundary cannot otherwise
  // tell a fresh call from a nested interrupt returning, and would
  // dispatch AH all over again).
  cpu.regs().ip = service::continuation_offset(break_continuation_slot);
  cpu.deliver_interrupt(ctrl_break_vector);
  return true;
}

// --- Console output: AH=02h, AH=09h, AH=40h on handles 1/2 -------------

void console_char_out(service_floor& floor) {
  if (break_intercepted(floor)) {
    return;
  }
  const std::uint8_t dl = floor.box().processor().regs().get(reg8::dl);
  floor.box().console().put(dl);
  succeed(floor);
}

void print_string(service_floor& floor) {
  if (break_intercepted(floor)) {
    return;
  }
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  std::uint16_t offset = cpu.regs()[reg16::dx];

  // One full segment's worth of bytes is the bound: real DOS has none and
  // will walk memory until it finds a '$', but a test or a malformed
  // program with no terminator must not hang the machine.
  for (unsigned i = 0; i < 0x10000u; ++i) {
    const std::uint8_t byte = cpu.read_byte(segment, offset);
    if (byte == '$') {
      break;
    }
    floor.box().console().put(byte);
    ++offset;
  }
  succeed(floor);
}

void write_console(service_floor& floor, std::uint16_t count) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];

  std::array<std::uint8_t, io_chunk_size> chunk{};
  std::uint16_t total = 0;
  while (total < count) {
    const std::size_t want =
        chunk_len(static_cast<std::uint16_t>(count - total));
    for (std::size_t i = 0; i < want; ++i) {
      chunk[i] = cpu.read_byte(segment,
                               static_cast<std::uint16_t>(offset + total + i));
    }
    floor.box().console().write(
        std::span<const std::uint8_t>(chunk.data(), want));
    total = static_cast<std::uint16_t>(total + want);
  }
}

// --- File I/O: AH=39h, 3Ch, 3Dh, 3Eh, 3Fh, 40h, 41h, 42h ----------------

void read_file(service_floor& floor, filesystem& fs, file_handle backing,
               std::uint16_t count) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];

  std::array<std::uint8_t, io_chunk_size> chunk{};
  std::uint16_t total = 0;
  while (total < count) {
    const std::size_t want =
        chunk_len(static_cast<std::uint16_t>(count - total));
    const vfs_result<std::size_t> got =
        fs.read(backing, std::span<std::uint8_t>(chunk.data(), want));
    if (!got.ok()) {
      if (total == 0) {
        fail(floor, got.error);
        return;
      }
      break;
    }
    for (std::size_t i = 0; i < got.value; ++i) {
      cpu.write_byte(segment, static_cast<std::uint16_t>(offset + total + i),
                     chunk[i]);
    }
    total = static_cast<std::uint16_t>(total + got.value);
    if (got.value < want) {
      break;  // Short read: end of file, still success (vfs.h).
    }
  }
  succeed_with(floor, total);
}

void write_file(service_floor& floor, filesystem& fs, file_handle backing,
                std::uint16_t count) {
  if (count == 0) {
    // DOS's own write-with-CX=0 convention: dos.h's top comment.
    const vfs_error err = fs.truncate(backing);
    if (err != vfs_error::none) {
      fail(floor, err);
      return;
    }
    succeed_with(floor, 0);
    return;
  }

  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];

  std::array<std::uint8_t, io_chunk_size> chunk{};
  std::uint16_t total = 0;
  while (total < count) {
    const std::size_t want =
        chunk_len(static_cast<std::uint16_t>(count - total));
    for (std::size_t i = 0; i < want; ++i) {
      chunk[i] = cpu.read_byte(segment,
                               static_cast<std::uint16_t>(offset + total + i));
    }
    const vfs_result<std::size_t> put =
        fs.write(backing, std::span<const std::uint8_t>(chunk.data(), want));
    if (!put.ok()) {
      if (total == 0) {
        fail(floor, put.error);
        return;
      }
      break;
    }
    total = static_cast<std::uint16_t>(total + put.value);
    if (put.value < want) {
      break;  // Short write: the backend has no more room (vfs.h).
    }
  }
  succeed_with(floor, total);
}

void mkdir_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  const vfs_result<dos_path> path = read_path(floor);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_error err = fs->mkdir(path.value);
  if (err != vfs_error::none) {
    fail(floor, err);
    return;
  }
  succeed(floor);
}

void create_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  const vfs_result<dos_path> path = read_path(floor);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_result<file_handle> opened = fs->create(path.value);
  if (!opened.ok()) {
    fail(floor, opened.error);
    return;
  }
  const std::uint16_t handle = floor.box().dos().open_file(opened.value);
  if (handle == dos_services::no_handle) {
    fs->close(opened.value);
    fail(floor, vfs_error::too_many_open_files);
    return;
  }
  succeed_with(floor, handle);
}

void open_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }

  // Bits 0-2 only — bits 3-7 are the reserved bit, the sharing mode and
  // the inheritance flag, none of which this subset models (dos.h's top
  // comment).
  const std::uint8_t mode_bits =
      floor.box().processor().regs().get(reg8::al) & 0x07u;
  if (mode_bits > 2) {
    fail(floor, invalid_access_code);
    return;
  }

  const vfs_result<dos_path> path = read_path(floor);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_result<file_handle> opened =
      fs->open(path.value, static_cast<open_mode>(mode_bits));
  if (!opened.ok()) {
    fail(floor, opened.error);
    return;
  }
  const std::uint16_t handle = floor.box().dos().open_file(opened.value);
  if (handle == dos_services::no_handle) {
    fs->close(opened.value);
    fail(floor, vfs_error::too_many_open_files);
    return;
  }
  succeed_with(floor, handle);
}

void close_fn(service_floor& floor) {
  const std::uint16_t handle = floor.box().processor().regs()[reg16::bx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr) {
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  const bool is_file = state->kind == dos_services::handle_kind::file;
  const file_handle backing = state->backing;

  floor.box().dos().close(handle);

  if (is_file) {
    // A handle this table holds as `file` can only exist because
    // create()/open() got a real `file_handle` from `vfs()` earlier, so
    // it cannot have gone null since — the check is defensive, not a
    // path this machine can actually reach.
    if (filesystem* fs = floor.box().vfs(); fs != nullptr) {
      fs->close(backing);
    }
  }
  succeed(floor);
}

void read_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t handle = cpu.regs()[reg16::bx];
  const std::uint16_t count = cpu.regs()[reg16::cx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr) {
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  if (state->kind != dos_services::handle_kind::file) {
    // Console and the documented sink have no input path yet (#53); the
    // honest answer is "no data", the same one end of file gives.
    succeed_with(floor, 0);
    return;
  }

  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  read_file(floor, *fs, state->backing, count);
}

void write_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t handle = cpu.regs()[reg16::bx];
  const std::uint16_t count = cpu.regs()[reg16::cx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr) {
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  switch (state->kind) {
    case dos_services::handle_kind::console:
      if (break_intercepted(floor)) {
        return;
      }
      write_console(floor, count);
      succeed_with(floor, count);
      return;
    case dos_services::handle_kind::null_sink:
      // Every byte accepted and discarded — dos.h's top comment.
      succeed_with(floor, count);
      return;
    case dos_services::handle_kind::file: {
      filesystem* fs = require_vfs(floor);
      if (fs == nullptr) {
        return;
      }
      write_file(floor, *fs, state->backing, count);
      return;
    }
  }
}

void unlink_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  const vfs_result<dos_path> path = read_path(floor);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_error err = fs->unlink(path.value);
  if (err != vfs_error::none) {
    fail(floor, err);
    return;
  }
  succeed(floor);
}

void seek_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint8_t al = cpu.regs().get(reg8::al);
  if (al > 2) {
    fail(floor, invalid_function_code);
    return;
  }

  const std::uint16_t handle = cpu.regs()[reg16::bx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr || state->kind != dos_services::handle_kind::file) {
    // Only real files are seekable in this subset — the console and the
    // documented sink are streams, not files with a position to move.
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }

  const auto origin = static_cast<seek_origin>(al);
  const std::uint16_t cx = cpu.regs()[reg16::cx];
  const std::uint16_t dx = cpu.regs()[reg16::dx];
  const auto offset = static_cast<std::int32_t>(pack32(cx, dx));

  const vfs_result<std::uint32_t> result =
      fs->seek(state->backing, origin, offset);
  if (!result.ok()) {
    fail(floor, result.error);
    return;
  }

  cpu.regs()[reg16::dx] = high_half(result.value);
  cpu.regs()[reg16::ax] = low_half(result.value);
  floor.set_caller_carry(false);
}

// --- Date/time: AH=2Ah, AH=2Ch ------------------------------------------

void get_date_fn(service_floor& floor) {
  machine& box = floor.box();
  const wall_time now = box.wall().at(box.time());
  cpu::registers& regs = box.processor().regs();
  regs[reg16::cx] = now.year;
  regs.set(reg8::dh, now.month);
  regs.set(reg8::dl, now.day);
  regs.set(reg8::al, now.weekday);
  floor.set_caller_carry(false);
}

void get_time_fn(service_floor& floor) {
  machine& box = floor.box();
  const wall_time now = box.wall().at(box.time());
  cpu::registers& regs = box.processor().regs();
  regs.set(reg8::ch, now.hour);
  regs.set(reg8::cl, now.minute);
  regs.set(reg8::dh, now.second);
  regs.set(reg8::dl, now.centisecond);
  floor.set_caller_carry(false);
}

// --- Exit: AH=4Ch, INT 20h -----------------------------------------------

void terminate(service_floor& floor, std::uint8_t code) {
  floor.box().dos().record_exit(code);
  floor.box().exit_program(code);
}

// --- The dispatcher itself -----------------------------------------------

void int21_dispatch(service_floor& floor, std::uint8_t /*vector*/) {
  const std::uint8_t ah = floor.box().processor().regs().get(reg8::ah);
  switch (ah) {
    case 0x02:
      console_char_out(floor);
      return;
    case 0x09:
      print_string(floor);
      return;
    case 0x2A:
      get_date_fn(floor);
      return;
    case 0x2C:
      get_time_fn(floor);
      return;
    case 0x39:
      mkdir_fn(floor);
      return;
    case 0x3C:
      create_fn(floor);
      return;
    case 0x3D:
      open_fn(floor);
      return;
    case 0x3E:
      close_fn(floor);
      return;
    case 0x3F:
      read_fn(floor);
      return;
    case 0x40:
      write_fn(floor);
      return;
    case 0x41:
      unlink_fn(floor);
      return;
    case 0x42:
      seek_fn(floor);
      return;
    case 0x4C:
      terminate(floor, floor.box().processor().regs().get(reg8::al));
      return;
    default:
      // Every other AH — 2Bh/2Dh (date/time setters) included, deferred
      // on purpose (dos.h's top comment) — refuses exactly as an
      // unbacked vector would (PLAN.md §3). The vector-level trace
      // service_floor::call() already logged AX and the caller's CS:IP
      // before this dispatcher ever ran; this is the stop that goes with
      // it.
      floor.box().stop_unimplemented_function(
          cpu::physical_address(floor.caller_cs(), floor.caller_ip()));
  }
}

void int20_terminate(service_floor& floor, std::uint8_t /*vector*/) {
  terminate(floor, 0);  // INT 20h has no exit code of its own.
}

}  // namespace

void install_dos_services(service_floor& floor) {
  floor.provide(0x20, &int20_terminate);
  floor.provide(0x21, &int21_dispatch);
  // The vector named here is 0x21, not 0x23: this continuation belongs to
  // the INT 21h call it finishes closing out, the same way the timer's
  // continuation is filed under the timer vector rather than under 1Ch,
  // the vector that actually returns into it (service_floor.h).
  floor.provide_continuation(break_continuation_slot, 0x21,
                             &break_continuation_done);
}

}  // namespace amberfolio::machine
