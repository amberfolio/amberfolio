// SPDX-License-Identifier: AGPL-3.0-only
//
// M2-D4 (#49): the PC speaker — port 61h (System Control Port B) gating
// PIT channel 2's tone into the host's audio pull. PLAN.md §3: "PIT
// channel 2 plus the port 61h gate, box-filtered into the host audio
// stream" — the one sound device of v1.
//
//
// Composition, mostly
// --------------------
//
// Everything virtual-time-to-samples is already built: platform.h's
// `audio_timeline` is a lock-free edge list plus a box filter, and
// `machine::audio()` is where it lives. Nothing here re-derives box
// filtering, resampling, or the producer/consumer contract — read
// platform.h's "Audio pull" section before touching this file if any of
// that looks unfamiliar. This device's entire job is to keep that edge
// list fed with the right edges at the right ticks.
//
// The other half already existed too, almost: pit.h (#46) computed
// channel 2's *count* on demand but never answered what its OUT *level*
// was, because nothing needed to know until now. `pit_channel::output()`
// and `next_output_change()`, added to pit.h alongside this issue, are
// that answer — a pure function of virtual time, exactly like
// `live_count()`. This device only reads them, gated by port 61h bit 1,
// and is itself a `scheduled` participant so it wakes up exactly when the
// gated result would change and publishes that one edge — never polling,
// and costing nothing on a step that changes nothing.
//
//
// Port 61h: the two bits that matter, and the ones that do not
// -----------------------------------------------------------------
//
// Bit 0 drives PIT channel 2's GATE input — `pit::set_gate2()`, #46's own
// door for exactly this. Bit 1 is the speaker data enable. The speaker
// cone is wired to their AND, through channel 2's OUT line:
//
//     speaker_level(at) = bit1 && pit.channel2_output(at)
//
// That single formula is both use cases PLAN.md §3 names:
//
//   * **A tone.** Bit 0 high leaves channel 2 counting, so its OUT line
//     is the square wave `output()` computes — period equal to the
//     programmed divisor, in ticks (pit.h's own doc comment on
//     `output()` has the argument for why). Bit 1 high lets it through.
//   * **Direct-drive sampled effects.** Bit 0 low forces channel 2's OUT
//     permanently high (the Intel 8253 datasheet's GATE-low rule, which
//     `output()` already implements), so the AND becomes `bit1 && true`
//     — the speaker plays bit 1 itself. A program toggling bit 1 in
//     software with the gate held low is driving the cone directly, the
//     era technique PLAN.md §3 has in mind. `resync()` below does not
//     know or care which case produced an edge; that is the entire point
//     of funnelling both through one formula and one publish call.
//
// Reads answer bits 0-1 with whatever was last written to them, which the
// issue's own text requires. This device keeps its own copy of them
// rather than asking the PIT for the gate back, because bit 1 (the data
// enable) is not state the PIT has any reason to know about, and reading
// two different objects for two bits of the same byte would only be able
// to disagree with itself later.
//
// Bits 4 and 7 are the well-documented static bits of a PC/XT's System
// Control Port B — bit 4 toggles with DRAM refresh, bit 7 latches a RAM
// parity-check NMI — and this machine simulates neither a refresh cycle
// nor a parity checker, so both read back fixed at 0 rather than
// pretending to a value nothing here produces; a write to either is
// accepted and simply has no effect, which is what a read-only status bit
// does on real hardware too. Every other bit (2, 3, 5, 6 — the parity- and
// I/O-channel-check *enable* bits, and a reserved one) is outside this
// subset: PLAN.md §3's discipline applies exactly as it does in pit.h and
// pic.h, and a write that sets any of them is `report_fault()`
// (device.h, #65) — a loud log line and a clean stop, not a guess, on the
// first such write (a faulted machine stops, so there is never a second
// time to log).
//
//
// Silence is exact
// -----------------
//
// `speaker_level(at)` is a plain boolean formula, so "silent" is never a
// special case this device has to detect — bit 1 low makes it constant
// false regardless of what channel 2 is doing, and a constant produces no
// further edges for `on_deadline()` to schedule. The one edge that *is*
// published is the transition into or out of that state, at the exact
// tick port 61h was written, and `audio_timeline` box-filters it like any
// other edge rather than as a special case — which is what keeps a mute
// or an unmute from sounding like a click.
//
//
// Reprogramming channel 2 out from under an armed deadline
// --------------------------------------------------------------
//
// `output()`/`next_output_change()` are pure functions, exact as of the
// moment they are asked — but this device's own scheduled deadline can
// still go stale: a program that rewrites channel 2's divisor (a fresh
// note, most commonly) does it through the PIT's own ports, not this
// one, and the write lands on a tick this device was not watching for.
// `pit_channel_observer` (pit.h) is the fix — this device registers as
// channel 2's one listener and `resync()`s whenever it fires, which is
// unconditional and therefore sometimes a no-op (a deferred reload does
// not change *this instant's* answer; `next_output_change()` already
// accounts for it on its own, so the observer finding nothing to
// republish there is the ordinary case, not a bug).

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/scheduler.h"

namespace amberfolio::machine {

class machine;

/// 61h: the System Control Port B this device answers for.
inline constexpr std::uint16_t speaker_port = 0x61;

/// The PC speaker: port 61h, plus the edge publisher that turns channel
/// 2's gated OUT line into `machine::audio()`'s edge list. See this
/// file's top comment.
class speaker final : public device,
                      public scheduled,
                      public pit_channel_observer {
 public:
  /// `box` must outlive this, and so must `timer` — the PIT channel 2
  /// gate (`pit::set_gate2()`) and the two pure queries
  /// (`pit::channel2_output()`, `pit::channel2_next_output_change()`)
  /// this device reads instead of ever touching a `pit_channel` directly.
  /// Registers itself as channel 2's one reprogram listener (this file's
  /// top comment, "Reprogramming channel 2...") as part of construction,
  /// so a machine wiring this up need only say
  /// `speaker spk(box, thepit); box.attach(spk); box.schedule(spk);` —
  /// three calls, the same shape every other device in this tree uses.
  speaker(machine& box, pit& timer) noexcept;

  static constexpr port_range port_window{.first = speaker_port,
                                          .last = speaker_port};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&port_window, 1)};
  }

  /// Power-on: both bits low — a real PC/XT's System Control Port B
  /// before anything has written it — which this also has to tell the
  /// PIT, and is the one piece of this device's own reset that is not
  /// simply "go back to the fields' default values": `pit_channel::
  /// reset()` does not know which channel it is (pit.h's own doc
  /// comment) and leaves every channel's gate high, the state channels 0
  /// and 1 are actually wired to. Channel 2's gate is wired to this
  /// device, not tied high, so this corrects it every time the RESET line
  /// runs, not only at construction.
  void reset() override;

  [[nodiscard]] std::uint8_t read_port(std::uint16_t port) override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  /// This device's own next-edge deadline (this file's top comment).
  void on_deadline(ticks due) override;

  /// Channel 2 was reprogrammed or its gate moved (pit.h's
  /// `pit_channel_observer`). Not part of the bus or the scheduler
  /// contract — the PIT calls this directly, which is why it is public
  /// on an otherwise bus-shaped device.
  void on_channel2_changed(ticks at) noexcept override;

 private:
  // --- The bits this device gives meaning to ---------------------------

  static constexpr std::uint8_t gate_bit = 0x01;
  static constexpr std::uint8_t data_enable_bit = 0x02;
  static constexpr std::uint8_t refresh_detect_bit = 0x10;
  static constexpr std::uint8_t parity_check_bit = 0x80;

  /// Everything this subset does not model (this file's top comment):
  /// bits 2, 3, 5 and 6. A write that sets any of them faults.
  static constexpr std::uint8_t undocumented_bits = static_cast<std::uint8_t>(
      ~(gate_bit | data_enable_bit | refresh_detect_bit | parity_check_bit));

  [[nodiscard]] ticks now() const noexcept;

  /// `bit1 && channel 2's OUT line` — the AND this file's top comment
  /// derives, at whatever tick the caller wants it for.
  [[nodiscard]] bool speaker_level(ticks at) const noexcept;

  /// Publish an edge if `speaker_level(at)` differs from what this device
  /// last published, and arm (or disarm) the next deadline either way.
  /// Called from every path whose answer could depend on it — a port
  /// write, this device's own `on_deadline()`, and channel 2 being
  /// reprogrammed out from under an already-armed one — the same "every
  /// path that could change the answer" discipline pit.h's `catch_up()`
  /// follows.
  void resync(ticks at);

  machine* box_;
  pit* timer_;

  /// Bits 0-1 only, as last written — what `read_port()` answers (this
  /// file's top comment on why this and not the PIT's own gate is the
  /// source of truth for bit 0's readback).
  std::uint8_t control_{0};

  /// What this device last told `machine::audio()`. Compared against
  /// `speaker_level()` on every `resync()` so an edge is only published
  /// when the answer actually changed — `audio_timeline::publish()`
  /// refuses a repeated tick anyway, but computing that once here is
  /// cheaper than asking it to refuse.
  bool last_level_{false};
};

}  // namespace amberfolio::machine
