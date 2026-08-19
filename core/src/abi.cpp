// SPDX-License-Identifier: AGPL-3.0-only
//
// The C ABI implementation. It is a translation of the C++ API and holds
// no logic of its own — that is the deal: the boundary stays thin enough
// that nothing can be true on one side of it and false on the other.
//
// Which means the rules are: every function here is a null check, a range
// check, a cast, and one call into machine/ or platform.h. Anything that
// needed a decision was a decision for platform.h to make, and if a
// question can only be answered by reading this file then the C++ side
// has a hole in it.

#include "amberfolio/abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/version.h"

/// The handle's definition, which is a machine and nothing else.
///
/// A wrapper struct rather than `typedef machine af_machine`, because the
/// C header declares `struct af_machine` and C has no idea what a
/// namespace is; this is the one place the two spellings meet.
struct af_machine {
  amberfolio::machine::machine box;
};

namespace {

using amberfolio::machine::key_action;
using amberfolio::machine::machine;
using amberfolio::machine::speed_preset;
using amberfolio::machine::stop_reason;
using amberfolio::machine::wall_time;

// The packing in abi.h gives each component 8 bits. Nothing enforces that
// on the project version, so state it here rather than silently truncating
// at 0.0.256.
constexpr bool fits_in_a_byte(int n) noexcept { return n >= 0 && n <= 0xFF; }

static_assert(fits_in_a_byte(amberfolio::core_version.major) &&
                  fits_in_a_byte(amberfolio::core_version.minor) &&
                  fits_in_a_byte(amberfolio::core_version.patch),
              "af_version() packs each version component into 8 bits; this "
              "project version does not fit. Widen the packing in abi.h.");

constexpr uint32_t byte_of(int n) noexcept {
  return static_cast<uint32_t>(n) & 0xFFu;
}

// The C macros against the C++ enums they restate. A host reading abi.h
// and a handler reading clock.h have to mean the same thing by "turbo
// XT", and there is no compiler that would otherwise notice.
static_assert(AF_SPEED_PC_XT == static_cast<uint32_t>(speed_preset::pc_xt));
static_assert(AF_SPEED_TURBO_XT ==
              static_cast<uint32_t>(speed_preset::turbo_xt));
static_assert(AF_SPEED_AT == static_cast<uint32_t>(speed_preset::at));
static_assert(AF_OK == static_cast<uint32_t>(stop_reason::none),
              "af_machine_stop_reason() returns machine::stop_reason "
              "directly, so a running machine's reason and AF_OK have to be "
              "the same number.");

/// The one machine, in static storage.
///
/// core/ does not allocate (PLAN.md §4), and `af_machine_create` has to
/// produce a megabyte-sized object; the way to have both is a buffer that
/// is already there and a placement new into it. See abi.h's "The handle"
/// section for why one is the right number.
///
/// Uninitialized on purpose: it is .bss, so it costs the wasm module no
/// bytes on the wire, and constructing it before `create` is asked for
/// would mean a machine existing that nobody made.
alignas(af_machine) std::array<std::byte, sizeof(af_machine)> storage;
af_machine* live = nullptr;

/// The one accessor. Every entry point below starts by calling it, which
/// is what makes "a null handle answers rather than traps" true by
/// construction rather than by remembering.
machine* box_of(af_machine* handle) noexcept {
  return handle == nullptr ? nullptr : &handle->box;
}

const machine* box_of(const af_machine* handle) noexcept {
  return handle == nullptr ? nullptr : &handle->box;
}

}  // namespace

extern "C" {

uint32_t af_version(void) {
  const amberfolio::version v = amberfolio::linked_version();
  return (byte_of(v.major) << 16) | (byte_of(v.minor) << 8) | byte_of(v.patch);
}

double af_ticks_per_second(void) {
  return static_cast<double>(amberfolio::machine::pit_input_hz);
}

uint32_t af_frame_width(void) { return amberfolio::machine::frame_width; }

uint32_t af_frame_height(void) { return amberfolio::machine::frame_height; }

uint32_t af_palette_entries(void) {
  return amberfolio::machine::palette_entries;
}

af_machine* af_machine_create(void) {
  if (live != nullptr) {
    return nullptr;
  }
  // Default-initialized, not `af_machine{}`: `machine` has an explicit
  // default constructor, which aggregate initialization may not call.
  live = ::new (static_cast<void*>(storage.data())) af_machine;
  return live;
}

void af_machine_destroy(af_machine* handle) {
  if (handle == nullptr || handle != live) {
    return;
  }
  live->~af_machine();
  live = nullptr;
}

uint32_t af_machine_reset(af_machine* handle) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  box->reset();
  return AF_OK;
}

uint32_t af_machine_run_until(af_machine* handle, double tick) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (tick < 0.0) {
    return AF_INVALID;
  }
  box->run(static_cast<amberfolio::machine::ticks>(tick));
  return box->stopped() ? AF_STOPPED : AF_OK;
}

double af_machine_time(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->time());
}

int32_t af_machine_stopped(const af_machine* handle) {
  const machine* box = box_of(handle);
  return (box != nullptr && box->stopped()) ? 1 : 0;
}

uint32_t af_machine_stop_reason(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  return static_cast<uint32_t>(box->stop().reason);
}

uint32_t af_machine_set_speed(af_machine* handle, uint32_t preset) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (preset > AF_SPEED_AT) {
    return AF_INVALID;
  }
  box->set_speed(static_cast<speed_preset>(preset));
  return AF_OK;
}

const uint8_t* af_machine_framebuffer(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? nullptr : box->display().pixels().data();
}

const uint8_t* af_machine_palette(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return nullptr;
  }
  // Three bytes per entry, in memory order, which is what `rgb` already
  // is — the reinterpretation is a fact about the layout, asserted rather
  // than assumed.
  static_assert(sizeof(amberfolio::machine::rgb) == 3);
  static_assert(alignof(amberfolio::machine::rgb) == 1);
  return reinterpret_cast<const uint8_t*>(box->display().palette().data());
}

double af_machine_frame_generation(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0
                        : static_cast<double>(box->display().generation());
}

uint32_t af_machine_render_audio(af_machine* handle, float* out,
                                 uint32_t frames, uint32_t sample_rate) {
  machine* box = box_of(handle);
  if (box == nullptr || out == nullptr) {
    return 0;
  }
  const std::span<float> buffer(out, frames);
  return static_cast<uint32_t>(box->audio().render(buffer, sample_rate));
}

double af_machine_audio_underruns(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->audio().underruns());
}

double af_machine_audio_resyncs(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->audio().resyncs());
}

uint32_t af_machine_post_key(af_machine* handle, uint32_t scancode,
                             int32_t down) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  // The 0x80 bit is the release bit on the wire and `down` carries it
  // here, so a code with it set is a host that has not read the contract.
  if (scancode == 0 || scancode > 0x7Fu) {
    return AF_INVALID;
  }
  box->post_key(static_cast<std::uint8_t>(scancode),
                down != 0 ? key_action::down : key_action::up);
  return AF_OK;
}

uint32_t af_machine_set_wall_clock(af_machine* handle, uint32_t year,
                                   uint32_t month, uint32_t day, uint32_t hour,
                                   uint32_t minute, uint32_t second,
                                   uint32_t centisecond) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (year > 0xFFFFu || month > 0xFFu || day > 0xFFu || hour > 0xFFu ||
      minute > 0xFFu || second > 0xFFu || centisecond > 0xFFu) {
    return AF_INVALID;
  }

  const wall_time when{.year = static_cast<std::uint16_t>(year),
                       .month = static_cast<std::uint8_t>(month),
                       .day = static_cast<std::uint8_t>(day),
                       .weekday = 0,
                       .hour = static_cast<std::uint8_t>(hour),
                       .minute = static_cast<std::uint8_t>(minute),
                       .second = static_cast<std::uint8_t>(second),
                       .centisecond = static_cast<std::uint8_t>(centisecond)};
  return box->set_wall_time(when) ? AF_OK : AF_INVALID;
}

uint32_t af_machine_read_console(af_machine* handle, uint8_t* out,
                                 uint32_t max) {
  machine* box = box_of(handle);
  if (box == nullptr || out == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(
      box->console().read(std::span<uint8_t>(out, max)));
}

uint32_t af_machine_console_pending(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(box->console().pending());
}

double af_machine_console_dropped(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return 0.0;
  }
  return static_cast<double>(box->console().dropped());
}

uint32_t af_machine_load_program(af_machine* handle, const uint8_t* image,
                                 uint32_t size) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (image == nullptr || size == 0) {
    return AF_INVALID;
  }
  // Reserved: the MZ loader is M2-D6 (#51). Loud rather than a quiet
  // success, per PLAN.md §3 — the whole point of the code is that a host
  // calling it early cannot mistake nothing for something.
  return AF_UNIMPLEMENTED;
}

uint32_t af_machine_write_memory(af_machine* handle, uint32_t address,
                                 const uint8_t* bytes, uint32_t size) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (bytes == nullptr) {
    return AF_INVALID;
  }
  const std::span<std::uint8_t> ram = box->memory().ram();
  if (address > ram.size() || size > ram.size() - address) {
    return AF_INVALID;
  }

  const std::span<const std::uint8_t> source(bytes, size);
  for (std::size_t i = 0; i < source.size(); ++i) {
    ram[address + i] = source[i];
  }
  return AF_OK;
}

uint32_t af_machine_read_memory(af_machine* handle, uint32_t address,
                                uint8_t* out, uint32_t size) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (out == nullptr) {
    return AF_INVALID;
  }
  const std::span<const std::uint8_t> ram = box->memory().ram();
  if (address > ram.size() || size > ram.size() - address) {
    return AF_INVALID;
  }

  const std::span<std::uint8_t> destination(out, size);
  for (std::size_t i = 0; i < destination.size(); ++i) {
    destination[i] = ram[address + i];
  }
  return AF_OK;
}

uint32_t af_machine_set_entry(af_machine* handle, uint32_t cs, uint32_t ip,
                              uint32_t ss, uint32_t sp) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (cs > 0xFFFFu || ip > 0xFFFFu || ss > 0xFFFFu || sp > 0xFFFFu) {
    return AF_INVALID;
  }

  amberfolio::cpu::registers& regs = box->processor().regs();
  regs[amberfolio::cpu::sreg::cs] = static_cast<std::uint16_t>(cs);
  regs.ip = static_cast<std::uint16_t>(ip);
  regs[amberfolio::cpu::sreg::ss] = static_cast<std::uint16_t>(ss);
  regs[amberfolio::cpu::reg16::sp] = static_cast<std::uint16_t>(sp);
  return AF_OK;
}

}  // extern "C"
