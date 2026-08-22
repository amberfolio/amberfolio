// SPDX-License-Identifier: AGPL-3.0-only
//
// The canonical machine-state serialization, and the hash of it that a
// replay golden is (M4-R1, #100).
//
// PLAN.md §4: "a replay that also captures the initial conditions
// reproduces a run exactly. Replay goldens hash a canonical, versioned
// machine-state serialization; floating-point audio output is excluded."
// This is that serialization. It is not a save-state format — machine
// save-states are out of v1 (PLAN.md §9) — it is the byte layout a hash
// is taken over, and the only thing that matters about it is that two
// machines in the same state produce the same bytes, on every target, in
// every build, for as long as `state_format_version` stays the same.
//
//
// What is in it, and what is not
// ------------------------------
//
// **In**: everything the program can observe or that decides what it
// observes next — the processor's architectural state and its interrupt
// latches; the virtual clock and the step count; every byte of RAM;
// every attached device's architectural state, in attach order; the
// scheduler's armed deadlines, in registration order; the keyboard
// service's lock bookkeeping; the DOS handle table and the position of
// every open file; the wall clock's seed; the input events still queued;
// the console bytes not yet drained; the speaker's edge list, as a count
// and a running digest of every edge published since reset; the
// framebuffer and its generation; and the stop record.
//
// **Out**, by decision: the speed governor (configuration — a replay
// records it as an initial condition, PLAN.md §4); the seam engine and
// its toggles (configuration, seam.h); the overlay tracker (observation
// of machine events, reconstructed by replaying them, overlay.h); the
// trace ring and the first-touch notice bitmaps (diagnostic bookkeeping,
// and a run with a sink and a run without one must hash the same); the
// audio timeline's consumer side and every float sample (output, not
// state — platform.h); the filesystem's contents (the host's, captured as
// a manifest in the recording's initial conditions, and checked by
// fingerprint rather than hashed per checkpoint).
//
// The sections are fixed and named (`state_section`) so a divergence can
// say *where* two runs first disagreed — "the EGA, at checkpoint 3" is a
// finding, a changed whole-state hash is a shrug.
//
//
// Versioned, deliberately
// -----------------------
//
// `state_format_version` changes when the layout changes — a device
// grows a register, a section is added — and every golden depends on it,
// so a bump is a decision recorded in docs/replay.md and a re-recording
// of the session library (#101), never a side effect. A recording names
// the version it was hashed under; a player refuses to compare across
// versions rather than report a divergence that is really a format
// change.
//
//
// How a device takes part
// -----------------------
//
// `device::save_state(state_sink&)` (device.h) — every device writes its
// own architectural state, in a fixed order, through the same writer the
// machine uses for its own. Registers and counters go in as fixed-width
// little-endian integers (`state_sink::u8/u16/u32/u64`), so the layout
// does not depend on the host's endianness or any struct's padding.
// Nothing else about a device is the serialization's business: the
// device knows what it is, and the machine only knows the order.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/sha256.h"

namespace amberfolio::machine {

class machine;

/// The layout version. Bump it, and only it, when the bytes change.
inline constexpr std::uint32_t state_format_version = 1;

/// The sections, in the order they are written. Named so a divergence
/// report can say which.
enum class state_section : std::uint8_t {
  clock,
  cpu,
  ram,
  devices,
  scheduler,
  keyboard,
  dos,
  wall,
  input,
  console,
  audio,
  display,
  stop,
};

inline constexpr std::size_t state_section_count = 13;

/// The printable name of a section. Never null.
[[nodiscard]] const char* state_section_name(state_section which) noexcept;

/// Where the serialization goes: a sink of sections and bytes. The hasher
/// below is one; a test that wants to look at the bytes is another.
class state_sink {
 public:
  state_sink() = default;
  state_sink(const state_sink&) = delete;
  state_sink(state_sink&&) = delete;
  state_sink& operator=(const state_sink&) = delete;
  state_sink& operator=(state_sink&&) = delete;

  /// A new section begins. Everything until the next `begin()` belongs to
  /// `which`.
  virtual void begin(state_section which) = 0;

  /// Raw bytes, in the order the machine produced them.
  virtual void bytes(std::span<const std::uint8_t> data) = 0;

  /// Fixed-width little-endian integers, so the layout is the same on
  /// every host. Implemented here once, over `bytes()`.
  void u8(std::uint8_t value);
  void u16(std::uint16_t value);
  void u32(std::uint32_t value);
  void u64(std::uint64_t value);
  void flag(bool value) { u8(value ? 1 : 0); }

 protected:
  ~state_sink() = default;
};

/// The hashes of one serialization: the whole, and each section on its
/// own. Two of these compare equal exactly when the bytes did.
struct state_hashes {
  sha256_digest whole{};
  std::array<sha256_digest, state_section_count> sections{};

  friend constexpr bool operator==(const state_hashes&,
                                   const state_hashes&) = default;
};

/// A sink that hashes: the whole stream into `whole`, each section into
/// its own slot as it goes by. Constructed empty; `finish()` once.
class state_hasher final : public state_sink {
 public:
  void begin(state_section which) override;
  void bytes(std::span<const std::uint8_t> data) override;

  /// Close the last section and answer the hashes. Call once.
  [[nodiscard]] state_hashes finish();

 private:
  void close_section();

  sha256_hasher whole_;
  sha256_hasher section_;
  state_hashes out_{};
  bool in_section_{false};
  state_section current_{};
};

/// Write `box`'s state into `out`, section by section, in the canonical
/// layout. Const: nothing about the machine changes, and the serialization
/// of a machine is the same before and after taking it.
void serialize_state(const machine& box, state_sink& out);

/// The hashes of `box`'s state right now — `serialize_state` through a
/// `state_hasher`, which is what a checkpoint records.
[[nodiscard]] state_hashes hash_state(const machine& box);

}  // namespace amberfolio::machine
