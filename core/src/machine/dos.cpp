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
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

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

  dta_ = default_dta;
  searched_ = {};
  searched_count_ = 0;
}

void dos_services::close_files(filesystem& fs) {
  for (std::uint16_t i = 0; i < max_handles; ++i) {
    if (handles_[i].in_use && handles_[i].kind == handle_kind::file) {
      fs.close(handles_[i].backing);
      close(i);
    }
  }
}

std::uint16_t dos_services::open_file(file_handle file,
                                      const dos_path& path) noexcept {
  for (std::uint16_t i = 0; i < max_handles; ++i) {
    if (!handles_[i].in_use) {
      handles_[i] = {.in_use = true,
                     .kind = handle_kind::file,
                     .backing = file,
                     .path = path};
      read_through_[i] = false;
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
  read_through_[handle] = false;
  return true;
}

bool dos_services::read_through(std::uint16_t handle) const noexcept {
  return handle < max_handles && read_through_[handle];
}

void dos_services::note_read(std::uint16_t handle) noexcept {
  if (handle < max_handles && handles_[handle].in_use) {
    read_through_[handle] = true;
  }
}

void dos_services::note_written(std::uint16_t handle) noexcept {
  if (handle < max_handles && handles_[handle].in_use) {
    handles_[handle].written = true;
  }
}

const dos_services::handle_state* dos_services::find(
    std::uint16_t handle) const noexcept {
  if (handle >= max_handles || !handles_[handle].in_use) {
    return nullptr;
  }
  return &handles_[handle];
}

vfs_error dos_services::set_current_directory(const filesystem& fs,
                                              const dos_path& directory) {
  if (!directory.is_root()) {
    const vfs_result<file_stat> found = fs.stat(directory);
    if (!found.ok() || !found.value.is_directory) {
      return vfs_error::path_not_found;
    }
  }
  current_directory_ = directory;
  return vfs_error::none;
}

namespace {

/// A path, component by component, each its length and then its text.
void save_path(state_sink& out, const dos_path& path) {
  out.u8(static_cast<std::uint8_t>(path.depth()));
  for (std::size_t i = 0; i < path.depth(); ++i) {
    const std::span<const char> text = path.component(i).text();
    out.u8(static_cast<std::uint8_t>(text.size()));
    for (const char c : text) {
      out.u8(static_cast<std::uint8_t>(c));
    }
  }
}

}  // namespace

void dos_services::save_state(state_sink& out) const {
  for (const handle_state& slot : handles_) {
    out.flag(slot.in_use);
    out.u8(static_cast<std::uint8_t>(slot.kind));
    out.u32(slot.backing.slot);
    out.flag(slot.written);
    // The path, component by component: a file's identity to the
    // program is its name, and a handle reopened on a different file is
    // a different state.
    save_path(out, slot.path);
  }
  out.u8(exit_code_);
  out.flag(exited_);
}

std::uint16_t dos_services::search_directory(
    const dos_path& directory) noexcept {
  for (std::size_t i = 0; i < searched_count_; ++i) {
    if (searched_[i] == directory) {
      return static_cast<std::uint16_t>(i);
    }
  }
  if (searched_count_ == max_search_directories) {
    return no_search_directory;
  }
  searched_[searched_count_] = directory;
  return static_cast<std::uint16_t>(searched_count_++);
}

const dos_path* dos_services::searched_directory(
    std::uint16_t index) const noexcept {
  return index < searched_count_ ? &searched_[index] : nullptr;
}

void dos_services::save_search_state(state_sink& out) const {
  if (dta_ == default_dta && searched_count_ == 0) {
    return;  // Power-on: the bytes a run that never searched always wrote.
  }
  out.u16(dta_.segment);
  out.u16(dta_.offset);
  out.u8(static_cast<std::uint8_t>(searched_count_));
  for (std::size_t i = 0; i < searched_count_; ++i) {
    save_path(out, searched_[i]);
  }
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
/// current directory the host set before the load (dos.h's top comment,
/// #397). The program cannot change it — AH=3Bh/47h are not in this
/// subset — so this is the one place it is read.
///
/// `what` is the naming call this is being read for, and it is here
/// because a name that does not canonicalize at all — a drive letter that
/// is not C, a component no legal DOS short name can equal — fails before
/// the filesystem is ever consulted. Until #121 that was the one naming
/// failure this machine reported nowhere: the program asked for something
/// on `A:`, got a refusal indistinguishable from a missing file, and put
/// a "please insert the disk" message on the screen with nothing in the
/// log to say why. There is no canonical path to name, so the event
/// carries the root and `error` is what says what happened. (The root,
/// and not the current directory: the name resolved to nothing, and
/// naming the directory it was relative to would read as a path.)
/// The ASCIZ name at DS:DX, as the program wrote it.
struct raw_name {
  std::array<char, max_raw_path> text{};
  std::size_t length{};

  [[nodiscard]] std::span<const char> view() const noexcept {
    return {text.data(), length};
  }
};

[[nodiscard]] raw_name read_raw_name(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];

  raw_name raw;
  while (raw.length < max_raw_path) {
    const std::uint8_t byte =
        cpu.read_byte(segment, static_cast<std::uint16_t>(offset + raw.length));
    if (byte == 0) {
      break;
    }
    raw.text[raw.length] = static_cast<char>(byte);
    ++raw.length;
  }
  return raw;
}

[[nodiscard]] vfs_result<dos_path> read_path(service_floor& floor,
                                             file_action what) {
  const raw_name raw = read_raw_name(floor);
  const vfs_result<dos_path> resolved =
      canonicalize(floor.box().dos().current_directory(), raw.view());
  if (!resolved.ok()) {
    floor.report_file(what, dos_path{}, 0, resolved.error);
  }
  return resolved;
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

void read_file(service_floor& floor, filesystem& fs, std::uint16_t handle,
               const dos_services::handle_state& state, std::uint16_t count) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t segment = cpu.regs()[sreg::ds];
  const std::uint16_t offset = cpu.regs()[reg16::dx];
  const file_handle backing = state.backing;

  // Where in the file this read begins — one of the facts the overlay
  // tracker records about it (overlay.h, #97). Asked of the backend
  // rather than tracked here, so the DOS layer keeps no second copy of a
  // position the backend already owns. A backend that cannot say is a
  // backend whose reads are not recorded, which is the honest answer.
  const vfs_result<std::uint32_t> position =
      fs.seek(backing, seek_origin::current, 0);

  std::array<std::uint8_t, io_chunk_size> chunk{};
  std::uint16_t total = 0;
  sha256_hasher hasher;
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
    hasher.update(std::span<const std::uint8_t>(chunk.data(), got.value));
    total = static_cast<std::uint16_t>(total + got.value);
    if (got.value < want) {
      break;  // Short read: end of file, still success (vfs.h).
    }
  }

  // Recorded after the bytes are in memory and before the program is
  // answered, which is the moment the module became resident. The
  // tracker is bookkeeping below the fidelity boundary (overlay.h): it
  // reads nothing back and changes nothing about this call.
  if (position.ok() && total != 0) {
    floor.box().note_file_read(state.path, position.value, segment, offset,
                               total, hasher.finish());
  }
  if (total != 0) {
    // Bytes moved, so this handle has been read *through* and its close
    // can say so (dos.h's `read_through`). Nothing above it changed.
    floor.box().dos().note_read(handle);
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
  const vfs_result<dos_path> path = read_path(floor, file_action::mkdir);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_error err = fs->mkdir(path.value);
  floor.report_file(file_action::mkdir, path.value, 0, err);
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
  const vfs_result<dos_path> path = read_path(floor, file_action::create);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_result<file_handle> opened = fs->create(path.value);
  if (!opened.ok()) {
    floor.report_file(file_action::create, path.value, 0, opened.error);
    fail(floor, opened.error);
    return;
  }
  const std::uint16_t handle =
      floor.box().dos().open_file(opened.value, path.value);
  if (handle == dos_services::no_handle) {
    fs->close(opened.value);
    floor.report_file(file_action::create, path.value, 0,
                      vfs_error::too_many_open_files);
    fail(floor, vfs_error::too_many_open_files);
    return;
  }
  floor.report_file(file_action::create, path.value, handle, vfs_error::none);
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

  const vfs_result<dos_path> path = read_path(floor, file_action::open);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_result<file_handle> opened =
      fs->open(path.value, static_cast<open_mode>(mode_bits));
  if (!opened.ok()) {
    // Reported, not stopped: "does this save slot exist" is a question a
    // program asks by opening the file, and no is an answer (dos.h).
    floor.report_file(file_action::open, path.value, 0, opened.error);
    fail(floor, opened.error);
    return;
  }
  const std::uint16_t handle =
      floor.box().dos().open_file(opened.value, path.value);
  if (handle == dos_services::no_handle) {
    fs->close(opened.value);
    floor.report_file(file_action::open, path.value, 0,
                      vfs_error::too_many_open_files);
    fail(floor, vfs_error::too_many_open_files);
    return;
  }
  floor.report_file(file_action::open, path.value, handle, vfs_error::none);
  succeed_with(floor, handle);
}

void close_fn(service_floor& floor) {
  const std::uint16_t handle = floor.box().processor().regs()[reg16::bx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr) {
    floor.report_file(file_action::close, dos_path{}, handle,
                      vfs_error::invalid_handle);
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  const bool is_file = state->kind == dos_services::handle_kind::file;
  const file_handle backing = state->backing;
  // Read before `close()` frees the slot: the path is the whole reason a
  // close is in this channel at all (diagnostics.h), and the two traffic
  // flags beside it are the whole reason a close can say whether the file
  // was used or only looked at.
  const dos_path named = state->path;
  const bool was_written = state->written;
  const bool was_read = floor.box().dos().read_through(handle);

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
  floor.report_file(file_action::close, named, handle, vfs_error::none,
                    was_read, was_written);
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
  read_file(floor, *fs, handle, *state, count);
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
      // Before the transfer, not after: DOS's bit 6 records that the
      // handle has been written *through*, and a write that fails
      // part-way still touched the file.
      floor.box().dos().note_written(handle);
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
  const vfs_result<dos_path> path = read_path(floor, file_action::unlink);
  if (!path.ok()) {
    fail(floor, path.error);
    return;
  }
  const vfs_error err = fs->unlink(path.value);
  floor.report_file(file_action::unlink, path.value, 0, err);
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

// --- The drive: AH=19h --------------------------------------------------
//
// The current drive, zero-based. There is one, C (vfs.h, `only_drive`),
// so it is the answer, and not a choice: no function this layer has can
// select another, and a name on any other drive does not resolve.

void current_drive_fn(service_floor& floor) {
  floor.box().processor().regs().set(reg8::al, only_drive);
  succeed(floor);
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

// --- IOCTL: AH=44h AL=00h -----------------------------------------------
//
// "What is behind this handle?" — the question a C runtime asks about
// each of the standard handles on its way up, to decide whether to line
// buffer. DOS answers with the device-information word, and every bit of
// it below is a documented DOS bit reporting something this machine
// actually knows, not a value picked because a program liked it:
//
//   * A **console** handle (1, 2) is a character device that this
//     machine's console sink backs: bit 7 (character device) and bit 1
//     (the console output device).
//   * A **documented-sink** handle (0, 3, 4) is a character device with
//     nothing behind it, which is precisely DOS's own NUL: bit 7 and
//     bit 2.
//   * Neither reports bit 6. For a character device bit 6 clear means
//     "at end of file on input", and a read of either kind answers zero
//     bytes today (dos.h's top comment) — which is what end of file is.
//     When #53's keyboard grows a console input path, the handle that
//     gains it is where this stops being true, and it will be visible
//     here rather than hidden.
//   * A **file** handle reports bit 7 clear, the drive in bits 0-5
//     (`only_drive`; there is one), and bit 6 set until something has
//     been written through it — DOS's own "0 if the file has been
//     written to", which is why `handle_state::written` exists.
//   * Bit 14, "the device accepts IOCTL control strings" (AL=02h-05h),
//     stays clear because this layer has none of those functions.
//
// AL other than 00h is refused the way any other unimplemented function
// is. AL=01h (set device information) is the near neighbour and it is
// deliberately absent: this machine has no line discipline to switch
// between cooked and raw, so accepting the write would be storing a bit
// that changes nothing and reporting it back as if it had — the exact
// shape of the silent accommodation PLAN.md §3 forbids. If a boot asks
// for it, #86 is where that gets decided with the log line as evidence.

/// Bits of the device-information word this layer reports. Named
/// individually because the alternative is a magic constant that hides
/// which facts about the handle went into it.
constexpr std::uint16_t devinfo_console_output = 0x0002;
constexpr std::uint16_t devinfo_nul_device = 0x0004;
constexpr std::uint16_t devinfo_character_device = 0x0080;

/// Bit 6, under the name it has when the handle is a file. DOS overloads
/// it — on a character device the same bit means "not at end of file on
/// input", which is why nothing here sets it for one.
constexpr std::uint16_t devinfo_file_unwritten = 0x0040;

[[nodiscard]] std::uint16_t device_info_word(
    const dos_services::handle_state& state) noexcept {
  switch (state.kind) {
    case dos_services::handle_kind::console:
      return devinfo_character_device | devinfo_console_output;
    case dos_services::handle_kind::null_sink:
      return devinfo_character_device | devinfo_nul_device;
    case dos_services::handle_kind::file:
      return static_cast<std::uint16_t>(
          only_drive | (state.written ? 0u : devinfo_file_unwritten));
  }
  return 0;
}

void ioctl_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  if (cpu.regs().get(reg8::al) != 0x00) {
    floor.box().stop_unimplemented_function(
        cpu::physical_address(floor.caller_cs(), floor.caller_ip()));
    return;
  }

  const std::uint16_t handle = cpu.regs()[reg16::bx];
  const dos_services::handle_state* state = floor.box().dos().find(handle);
  if (state == nullptr) {
    fail(floor, vfs_error::invalid_handle);
    return;
  }

  const std::uint16_t info = device_info_word(*state);
  cpu.regs()[reg16::dx] = info;
  // DOS 3's answer, which also leaves the word in AX; DOS 2 documented AX
  // as destroyed, so a program written against either sees something it
  // is entitled to see.
  succeed_with(floor, info);
}

// --- Interrupt vectors: AH=25h, AH=35h ----------------------------------
//
// Over the real vector table, because that is where a hook actually
// lives (service_floor.h's top comment): a program that calls AH=25h and
// a program that stores four bytes at 0000:0084 itself have done the
// same thing, and these two functions exist to keep that true rather
// than to keep a table of their own beside it. Nothing here knows or
// cares whether the vector being read still points at one of our stubs.
//
// Neither function has a documented failure, and neither validates the
// vector number: every value of AL names a real four-byte entry inside
// the 1 KiB table, so there is nothing to refuse. CF is cleared the way
// every other function in this file reports success — DOS's own
// documentation says nothing about CF for either, and the one convention
// a caller here can rely on is the one the rest of this dispatcher uses.

void set_vector_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t at = cpu::vector_table_offset(cpu.regs().get(reg8::al));
  cpu.write_word(cpu::vector_table_segment, at, cpu.regs()[reg16::dx]);
  cpu.write_word(cpu::vector_table_segment, static_cast<std::uint16_t>(at + 2),
                 cpu.regs()[sreg::ds]);
  succeed(floor);
}

void get_vector_fn(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t at = cpu::vector_table_offset(cpu.regs().get(reg8::al));
  cpu.regs()[reg16::bx] = cpu.read_word(cpu::vector_table_segment, at);
  cpu.regs()[sreg::es] = cpu.read_word(cpu::vector_table_segment,
                                       static_cast<std::uint16_t>(at + 2));
  succeed(floor);
}

// --- Directory searches: AH=1Ah, 4Eh, 4Fh (dos.h's top comment) --------

/// Where each field of a search record sits in the DTA: DOS 3's layout,
/// which every later DOS keeps. The first 21 bytes are DOS's own, for
/// FindNext to resume from; the rest is the answer a program reads.
namespace dta_field {
constexpr std::uint16_t drive = 0x00;
constexpr std::uint16_t pattern = 0x01;  // Eleven bytes, fcb_name.
constexpr std::uint16_t search_attributes = 0x0C;
constexpr std::uint16_t next_entry = 0x0D;  // Word.
constexpr std::uint16_t directory = 0x0F;   // Word: a cluster on a disk.
constexpr std::uint16_t reserved = 0x11;    // Four bytes.
constexpr std::uint16_t reserved_length = 4;
constexpr std::uint16_t attribute = 0x15;
constexpr std::uint16_t time = 0x16;  // Word.
constexpr std::uint16_t date = 0x18;  // Word.
constexpr std::uint16_t size = 0x1A;  // Doubleword.
constexpr std::uint16_t name = 0x1E;  // ASCIZ, at most 13 bytes.
}  // namespace dta_field

/// The attribute bits a search reports or asks for. Hidden (02h) and
/// system (04h) never occur here, because nothing in this machine can
/// set them; they are named because the mask rule is about them.
constexpr std::uint8_t attribute_hidden = 0x02;
constexpr std::uint8_t attribute_system = 0x04;
constexpr std::uint8_t attribute_volume_label = 0x08;
constexpr std::uint8_t attribute_directory = 0x10;

/// DOS's rule for the mask in CX: a plain file (no bits, read-only or
/// archive) is always found, and a hidden, system or directory entry
/// only when the mask asks for that kind. A mask of exactly 08h asks for
/// the volume label alone, and this machine's one drive has none.
[[nodiscard]] bool attribute_admits(std::uint8_t mask,
                                    std::uint8_t attribute) noexcept {
  if (mask == attribute_volume_label) {
    return false;
  }
  constexpr std::uint8_t must_ask =
      attribute_hidden | attribute_system | attribute_directory;
  return (attribute & must_ask & ~mask) == 0;
}

/// One entry a search looks at, in the form the DTA reports it.
struct search_entry {
  fcb_name fcb{};
  std::array<char, dos_name::max_length> text{};
  std::size_t length{};
  std::uint32_t size{};
  std::uint8_t attribute{};
};

[[nodiscard]] search_entry dot_entry(std::size_t dots) noexcept {
  search_entry entry;
  entry.fcb.fill(' ');
  for (std::size_t i = 0; i < dots; ++i) {
    entry.fcb[i] = '.';
    entry.text[i] = '.';
  }
  entry.length = dots;
  entry.attribute = attribute_directory;
  return entry;
}

/// The `index`-th entry a search of `directory` looks at. Below the root
/// that is `.` and `..` and then the VFS's entries in its pinned order;
/// at the root, the VFS's entries alone, as on DOS. `no_more_files` past
/// the end, and for a directory that has gone since the search began.
[[nodiscard]] vfs_result<search_entry> entry_for_search(
    const filesystem& fs, const dos_path& directory, std::size_t index) {
  const std::size_t dots = directory.is_root() ? 0 : 2;
  if (index < dots) {
    return {.value = dot_entry(index + 1), .error = vfs_error::none};
  }
  const vfs_result<directory_entry> found =
      fs.entry_at(directory, index - dots);
  if (!found.ok()) {
    return {.error = vfs_error::no_more_files};
  }
  search_entry entry;
  entry.fcb = fcb_form(found.value.name);
  const std::span<const char> text = found.value.name.text();
  for (std::size_t i = 0; i < text.size(); ++i) {
    entry.text[i] = text[i];
  }
  entry.length = text.size();
  entry.size = found.value.is_directory ? 0 : found.value.size;
  entry.attribute = found.value.is_directory ? attribute_directory : 0;
  return {.value = entry, .error = vfs_error::none};
}

/// The DTA as a place to read and write, through the bus like any other
/// guest memory.
class dta_view {
 public:
  explicit dta_view(service_floor& floor)
      : cpu_(&floor.box().processor()), at_(floor.box().dos().dta()) {}

  [[nodiscard]] std::uint8_t byte(std::uint16_t field) const {
    return cpu_->read_byte(at_.segment, offset(field));
  }
  [[nodiscard]] std::uint16_t word(std::uint16_t field) const {
    return cpu_->read_word(at_.segment, offset(field));
  }
  void put_byte(std::uint16_t field, std::uint8_t value) const {
    cpu_->write_byte(at_.segment, offset(field), value);
  }
  void put_word(std::uint16_t field, std::uint16_t value) const {
    cpu_->write_word(at_.segment, offset(field), value);
  }

 private:
  [[nodiscard]] std::uint16_t offset(std::uint16_t field) const noexcept {
    return static_cast<std::uint16_t>(at_.offset + field);
  }

  cpu::processor* cpu_;
  dos_services::far_address at_;
};

/// Look at entries from `index` on, and report the first that matches in
/// the DTA, with the index after it stored for FindNext. `no_more_files`
/// when none does; the record's own fields are left as they were.
[[nodiscard]] vfs_error continue_search(
    service_floor& floor, const filesystem& fs, const dos_path& directory,
    const search_pattern& pattern, std::uint8_t mask, std::uint16_t index) {
  for (std::size_t at = index; at < 0xFFFFU; ++at) {
    const vfs_result<search_entry> entry = entry_for_search(fs, directory, at);
    if (!entry.ok()) {
      return vfs_error::no_more_files;
    }
    if (!pattern.matches(entry.value.fcb) ||
        !attribute_admits(mask, entry.value.attribute)) {
      continue;
    }
    const dta_view dta(floor);
    dta.put_word(dta_field::next_entry, static_cast<std::uint16_t>(at + 1));
    dta.put_byte(dta_field::attribute, entry.value.attribute);
    // No timestamps in this machine (vfs.h): zero, and not a made-up day.
    dta.put_word(dta_field::time, 0);
    dta.put_word(dta_field::date, 0);
    dta.put_word(dta_field::size, low_half(entry.value.size));
    dta.put_word(static_cast<std::uint16_t>(dta_field::size + 2),
                 high_half(entry.value.size));
    for (std::size_t i = 0; i < entry.value.length; ++i) {
      dta.put_byte(static_cast<std::uint16_t>(dta_field::name + i),
                   static_cast<std::uint8_t>(entry.value.text[i]));
    }
    dta.put_byte(
        static_cast<std::uint16_t>(dta_field::name + entry.value.length), 0);
    return vfs_error::none;
  }
  return vfs_error::no_more_files;
}

void set_dta_fn(service_floor& floor) {
  const cpu::registers& regs = floor.box().processor().regs();
  floor.box().dos().set_dta(
      {.segment = regs[sreg::ds], .offset = regs[reg16::dx]});
  succeed(floor);
}

void find_first_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  dos_services& dos = floor.box().dos();
  const auto mask = static_cast<std::uint8_t>(
      floor.box().processor().regs()[reg16::cx] & 0xFFU);

  // The directory is everything up to the last `\` (or a drive's `:`),
  // resolved like any other path; the pattern is what follows it.
  const raw_name raw = read_raw_name(floor);
  std::size_t split = 0;
  for (std::size_t i = 0; i < raw.length; ++i) {
    if (raw.text[i] == '\\' || (i == 1 && raw.text[i] == ':')) {
      split = i + 1;
    }
  }
  const std::span<const char> whole = raw.view();
  const vfs_result<dos_path> directory =
      canonicalize(dos.current_directory(), whole.first(split));
  if (!directory.ok()) {
    // Nothing resolved, so the root and the error (read_path's rule).
    floor.report_file(file_action::find, dos_path{}, 0, directory.error);
    fail(floor, directory.error);
    return;
  }
  if (!directory.value.is_root()) {
    const vfs_result<file_stat> found = fs->stat(directory.value);
    if (!found.ok() || !found.value.is_directory) {
      floor.report_file(file_action::find, directory.value, 0,
                        vfs_error::path_not_found);
      fail(floor, vfs_error::path_not_found);
      return;
    }
  }
  const vfs_result<search_pattern> pattern =
      search_pattern::parse(whole.subspan(split));
  if (!pattern.ok()) {
    floor.report_file(file_action::find, directory.value, 0, pattern.error);
    fail(floor, pattern.error);
    return;
  }

  const std::uint16_t slot = dos.search_directory(directory.value);
  if (slot == dos_services::no_search_directory) {
    // dos.h: a full table is a stop, never a reused slot.
    floor.box().stop_unsupported_request(
        cpu::physical_address(floor.caller_cs(), floor.caller_ip()));
    return;
  }

  // The record FindNext resumes from, in DOS's own shape. The drive is
  // the one-based number the FCB functions use: C is 3.
  const dta_view dta(floor);
  dta.put_byte(dta_field::drive, static_cast<std::uint8_t>(only_drive + 1));
  for (std::size_t i = 0; i < pattern.value.fcb().size(); ++i) {
    dta.put_byte(static_cast<std::uint16_t>(dta_field::pattern + i),
                 static_cast<std::uint8_t>(pattern.value.fcb()[i]));
  }
  dta.put_byte(dta_field::search_attributes, mask);
  dta.put_word(dta_field::next_entry, 0);
  dta.put_word(dta_field::directory, slot);
  for (std::uint16_t i = 0; i < dta_field::reserved_length; ++i) {
    dta.put_byte(static_cast<std::uint16_t>(dta_field::reserved + i), 0);
  }

  const vfs_error found =
      continue_search(floor, *fs, directory.value, pattern.value, mask, 0);
  floor.report_file(file_action::find, directory.value, 0, found);
  if (found != vfs_error::none) {
    fail(floor, found);
    return;
  }
  succeed(floor);
}

void find_next_fn(service_floor& floor) {
  filesystem* fs = require_vfs(floor);
  if (fs == nullptr) {
    return;
  }
  const dta_view dta(floor);
  // A record whose directory word names nothing this machine searched is
  // one FindFirst did not write, and DOS's answer to that is the same as
  // to a search that has run out.
  const dos_path* directory =
      floor.box().dos().searched_directory(dta.word(dta_field::directory));
  if (directory == nullptr) {
    fail(floor, vfs_error::no_more_files);
    return;
  }
  fcb_name bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<char>(
        dta.byte(static_cast<std::uint16_t>(dta_field::pattern + i)));
  }
  const vfs_error found = continue_search(
      floor, *fs, *directory, search_pattern::from_fcb(bytes),
      dta.byte(dta_field::search_attributes), dta.word(dta_field::next_entry));
  if (found != vfs_error::none) {
    fail(floor, found);
    return;
  }
  succeed(floor);
}

// --- Exit: AH=4Ch, INT 20h -----------------------------------------------

void terminate(service_floor& floor, std::uint8_t code) {
  // DOS closes what an ending program left open (dos.h, `close_files()`).
  if (filesystem* fs = floor.box().vfs(); fs != nullptr) {
    floor.box().dos().close_files(*fs);
  }
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
    case 0x19:
      current_drive_fn(floor);
      return;
    case 0x1A:
      set_dta_fn(floor);
      return;
    case 0x25:
      set_vector_fn(floor);
      return;
    case 0x2A:
      get_date_fn(floor);
      return;
    case 0x2C:
      get_time_fn(floor);
      return;
    case 0x35:
      get_vector_fn(floor);
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
    case 0x44:
      ioctl_fn(floor);
      return;
    case 0x4C:
      terminate(floor, floor.box().processor().regs().get(reg8::al));
      return;
    case 0x4E:
      find_first_fn(floor);
      return;
    case 0x4F:
      find_next_fn(floor);
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
