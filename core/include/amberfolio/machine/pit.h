// SPDX-License-Identifier: AGPL-3.0-only
//
// M2-D1 (#46): the 8253 PIT at 40h-43h — channel 0 (the system tick,
// PLAN.md §3), channel 1 (counts; its output is wired to nothing on a
// PC and is wired to nothing here either), and channel 2 (speaker tone,
// M2-D4/#49).
//
//
// The central design point: the PIT does not tick
// -------------------------------------------------
//
// A tempting, wrong implementation counts down a channel's register by
// one on every virtual-clock tick, the way the real chip's silicon does.
// It would be both slower — a decrement and a compare on every one of
// 1,193,182 ticks a virtual second, for three channels, whether or not
// anything is reading them — and *less* accurate: it would tie the
// counter's truth to how often something remembered to step it, which is
// exactly the kind of drift clock.h's whole design exists to rule out.
//
// Instead, a channel converts its programmed state into an *equation*.
// `active_since_` is the tick at which the counter held its full
// programmed value; `active_divisor_` is that value (65536 for a
// programmed 0 — the 0-means-65536 rule, applied once, here). The count
// at any later tick `t` is `active_divisor_ - ((t - active_since_) mod
// active_divisor_)` for the periodic modes, and the equivalent one-shot
// form for mode 0 (`live_count()` below). This is exact for any `t`,
// arbitrarily far in the future, computed in constant time, with no
// state to keep current in between — which is the whole payoff of
// counting virtual time in PIT input clocks (clock.h) rather than in
// something a channel's own arithmetic would have to convert.
//
// The scheduler (scheduler.h) is not what makes reads correct — the
// equation above does that on its own, and channel 1 (which never
// touches the scheduler at all) is the proof. The scheduler is what
// makes an *edge* happen without anyone polling for it: channel 0's
// output has to reach the PIC (pic.h) the moment it happens, not the
// next time a program's own code happens to touch a PIT port, and that
// is a real event a real device (the 8259) has to be told about. So
// channel 0 and channel 2 each arm one deadline for their own next
// output edge and recompute it — never re-derive it from scratch, never
// walk ticks — every time a write, a gate edge, or the deadline itself
// changes what that edge would be. Channel 2 had no consumer at the time
// its own deadline was wired up, but scheduler.h's own design commentary
// names "the PIT running channel 0 and channel 2 on different divisors"
// as the reason a participant can want a second, independent deadline,
// so channel 2 was wired up before reopening this file to add its
// consumer — `output()`, `next_output_change()` and
// `pit_channel_observer` below, for M2-D4/#49's speaker.
//
// Channel 1 gets neither a controller to notify nor a scheduler
// registration — "cheaper and truer than special-casing it away," per
// this issue: cheaper, because a deadline nobody consumes is a wasted
// scheduler slot (`scheduler::max_participants` is 8, not free); truer,
// because registering one anyway would be pretending channel 1's output
// goes somewhere when it does not. It still counts, correctly, on
// demand, through the exact same equation channels 0 and 2 use — the
// point being that "counts" and "is scheduled" are independent facts
// about a channel, and channel 1 is the case that proves it.
//
//
// One `pit_channel`, instantiated three times
// -----------------------------------------------
//
// All three channels share one implementation and differ only in two
// constructor arguments: whether an output edge should reach a PIC
// (only channel 0 gets one), and nothing else — the scheduler
// registration itself happens where the machine is wired up
// (`machine::schedule`), the same way `machine::attach` is, so this
// class does not need to know which channels the wiring code chose to
// register.
//
//
// Modes 0, 2 and 3; nothing else
// ---------------------------------
//
// Mode 0 (interrupt on terminal count): a one-shot. Loading a count
// starts it counting down immediately, whatever it was doing before;
// reaching zero fires one edge and the count keeps counting down through
// the 16-bit wrap forever after — real hardware behaviour, modelled
// because it costs nothing extra once the equation above exists, though
// nothing downstream cares about it past the first edge.
//
// Modes 2 and 3 (rate generator, square wave generator): periodic, one
// edge every `active_divisor_` ticks. They are given the identical
// timing model here — one edge per divisor-many ticks — because that is
// the fact both channel 0's IRQ0 cadence and this issue's divisor-to-
// period tests depend on; the difference between them on real silicon is
// the *shape* of the output waveform inside one period (a one-clock low
// pulse for mode 2, a duty cycle split near 50/50 for mode 3, including
// mode 3's well-known quirk of only ever showing software an even
// intermediate count, because the internal counter decrements by two
// while the output is high). Nothing this project's plan needs reads an
// intermediate count mid-cycle and depends on its exact value or
// parity — PLAN.md §3 rules out cycle-level timing fidelity as a goal
// for the CPU for the same reason — so that shape is not modelled, and
// mode 3's read-back quirk is a deliberately rejected piece of fidelity,
// not an oversight.
//
// Every other mode value (1, 4, 5) and the BCD counting bit: log-and-
// stop (`report_fault()`, device.h's own channel back to the machine —
// #65). Nothing in the SSI Gold Box toolchain has ever been seen to use
// them, and PLAN.md §3's discipline says a mode this project cannot
// exercise against anything is a mode this project does not get to
// guess about.
//
//
// The write-order state machine, and the count-latch command
// ---------------------------------------------------------------
//
// A control-word write (43h) selects a channel (SC), an access mode —
// latch (RW=00), low byte only, high byte only, or low-then-high
// (RW=01/10/11) — and, unless it is a latch command, a mode and the BCD
// bit. RW=00 does not touch the mode or access fields at all; it snap-
// shots the addressed channel's live count into `latched_value_` and
// leaves everything else exactly as it was — a program can latch mid-
// program without disturbing what it had already set up to read or
// write.
//
// The access mode governs both directions independently: `write_data()`
// tracks which half of a two-byte write is still owed
// (`awaiting_msb_`/`partial_write_`), and `read_data()` tracks which
// half of a two-byte read comes next (`next_read_is_msb_`) — the two
// state machines are separate because a program can latch and read while
// a write it started is still incomplete, or vice versa, and neither
// side should perturb the other. A second latch command that arrives
// before the first one's value has been fully read is *ignored* — real
// 8253 behaviour, and the reason "latch reads stable while counting
// continues" is exact rather than merely close: whatever is mid-read
// stays mid-read.
//
//
// Writing a new count while one is already running
// -----------------------------------------------------
//
// Mode 0: the new count reloads and restarts immediately, cancelling
// whatever cycle was in flight — real 8253 behaviour, and simple because
// mode 0 never repeats anyway.
//
// Modes 2 and 3: the very first load after a control word starts
// counting right away (there is no "current cycle" yet to defer to); a
// rewrite while a cycle is already running is held as `pending_divisor_`
// and takes effect only at that cycle's own natural boundary — so a
// program that reprograms the rate mid-flight does not perturb the tick
// already in progress. `catch_up()` is what applies a pending reload
// once its boundary has passed, and it is called from every path that
// needs an answer that could depend on it — a read, a write, a gate
// edge, and (eagerly, so a scheduled channel's own deadline is never
// stale) `on_deadline()` itself — rather than assuming whichever path
// happens to run first will have done it.
//
//
// The channel-2 gate line
// ---------------------------
//
// GATE is external to the counter — on a real PC it is bit 0 of port
// 61h, wired only to channel 2 (channels 0 and 1 are tied permanently
// high). Port 61h is M2-D4/#49's; this issue's job is only to expose the
// wire, which is `pit::set_gate2()`. Per the Intel 8253 datasheet, its
// effect depends on the mode it is asked of:
//
//   * Mode 0: GATE low *pauses* the count — the counter freezes at
//     whatever it held, and GATE high resumes counting from there. This
//     is `set_gate()`'s mode-0 branch, and it uses the exact same
//     "shift the reference tick forward by the paused duration" trick
//     the rest of this file uses for a reload, applied instead to a
//     pause: `active_since_ += (now - gate_paused_at_)`.
//   * Modes 2 and 3: GATE low forces the output high and disables
//     counting; GATE's *rising edge* reloads the counter and restarts
//     it — not a resume, a fresh cycle, exactly as a natural terminal
//     count would. `set_gate()`'s mode-2/3 branch calls the same
//     `start_cycle()` a natural boundary or an initial load would.
//
// A channel currently gated off is simply never armed on the scheduler
// (`start_cycle()` checks `gate_` before arming) — there is no deadline
// to fire while nothing is counting — and `live_count()` freezes its
// reference tick at the moment the gate went low rather than at `now`,
// so a read while gated answers the frozen value for as long as the
// gate stays low, however long that is.
//
//
// Channel 2's OUT line: `output()`, `next_output_change()`, and the
// reprogram observer
// ------------------------------------------------------------------------
//
// Nothing before M2-D4/#49 ever needed to know channel 2's OUT *level* —
// `on_deadline()` exists to raise an IRQ, which is edge-triggered and
// level-blind, so channel 0 never needed this either. The speaker does:
// it gates this line against port 61h bit 1, and platform.h's
// `audio_timeline` needs the exact tick every transition happens at, not
// a level sampled after the fact.
//
// `output(at)` is that level, computed the same way `live_count()` is —
// a pure function of the state already here, nothing new to keep
// current. Mode 0 answers the datasheet's own rule: low until the
// terminal count, high and pinned there ever after. Modes 2 and 3 answer
// with **a 50%-duty square wave, full period `active_divisor_` ticks** —
// the real mode 3 waveform (Intel 8253 datasheet: high for
// `ceil(divisor/2)`, low for the rest), used for mode 2 as well for the
// identical reason `on_deadline()`'s timing already is shared between
// them (this file's second section, above): the two only differ in a
// shape this project does not model, and mode 3's shape already *is* the
// symmetric wave a tone needs. This is what turns a programmed divisor
// into a tone at exactly `pit_input_hz / active_divisor_` — the fact
// every BIOS beep and every era sound routine already assumes, and the
// fact M2-D4/#49's exit criterion checks. GATE low forcing the output
// high (the rule two paragraphs up) falls out of the same function: it
// is the direct-drive path M2-D4/#49 uses for sampled effects, because
// with GATE held low this is a constant true and the speaker's output is
// then whatever port 61h bit 1 says, toggled by software.
//
// `next_output_change(at)` is `output()`'s companion: the next tick at
// which it would answer differently. The speaker is a `scheduled`
// participant that arms itself here rather than walking ticks or
// re-deriving the divisor arithmetic a second time — exactly the
// division of labour `on_deadline()`'s own doc comment describes for
// channel 0's IRQ.
//
// Both are pure functions of `at`, which is exact only if the state they
// read is current as of `at` — true whenever `at` is "now" and a write or
// a gate edge to *this* channel triggered the query, because every path
// that can change either answer already runs `catch_up()` or
// `start_cycle()` first. It is not automatically true when something
// *else* changes what the answer would have been — reprogramming channel
// 2 while the speaker's already-armed deadline is still ticking down to
// the *old* divisor's boundary — which is what `pit_channel_observer`
// exists to close: `write_control()`, `write_data()` and `set_gate()`
// each notify it, unconditionally, at their one true exit point,
// whether or not the write actually changed anything observable right
// now (a deferred reload, for instance, does not — `next_output_change()`
// already accounts for `has_pending_` on its own, so the observer finding
// nothing to do there is the common case, not a bug). Only channel 2 is
// ever given one; the parallel with `irq_` — channel 0's own one-listener
// door — is deliberate, and the reason this is not folded into `irq_`
// itself is that an edge-triggered IRQ and a level query are different
// contracts answering different questions.

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/scheduler.h"

namespace amberfolio::machine {

class machine;
class state_sink;

namespace pic {
class controller;
}  // namespace pic

/// 40h-43h: channel 0's, channel 1's and channel 2's data ports, and the
/// shared control-word register.
inline constexpr std::uint16_t pit_channel0_port = 0x40;
inline constexpr std::uint16_t pit_channel1_port = 0x41;
inline constexpr std::uint16_t pit_channel2_port = 0x42;
inline constexpr std::uint16_t pit_control_port = 0x43;

/// The three counting modes this PIT implements. `none` is the power-on
/// and post-control-word-before-first-load state: a channel with no
/// programmed divisor yet.
enum class pit_mode : std::uint8_t { none, mode0, mode2, mode3 };

/// The RW field of a control word, applied to both `write_data()` and
/// `read_data()`. `none` only appears transiently — a channel with no
/// control word yet — since a control word other than the latch command
/// always sets one of the other three.
enum class pit_access : std::uint8_t { none, lsb, msb, both };

/// Something that wants to know when channel 2's programming or gate
/// state changed — port 61h's speaker (M2-D4/#49), and this file's own
/// top comment ("Channel 2's OUT line") for why a query alone is not
/// enough. Never called for channels 0 or 1.
class pit_channel_observer {
 public:
  /// Channel 2's control word, its divisor, or its gate line just moved,
  /// as of `at` — always `box_->time()`, the tick the write or gate edge
  /// happened at. `output()` and `next_output_change()` may or may not
  /// disagree with what they last answered; finding out is this
  /// listener's job, not this channel's.
  virtual void on_channel2_changed(ticks at) noexcept = 0;

 protected:
  // See cpu/bus.h: held by pointer, never owned or deleted through this
  // type.
  ~pit_channel_observer() = default;
};

/// One 8253 counter. `pit` instantiates this three times (this file's
/// top comment); nothing about the type says which channel it is,
/// because nothing about the 8253's counting logic differs by channel —
/// only the wiring around it does.
class pit_channel final : public scheduled {
 public:
  /// `box` must outlive this. `irq` is the controller to notify of an
  /// output edge — only channel 0 is ever given one; null means "this
  /// channel's output reaches no IRQ line," which is channels 1 and 2
  /// (channel 2's edges reach the speaker instead, through `output()` and
  /// `pit_channel_observer` — an edge-triggered IRQ and a level query are
  /// different contracts, so channel 2 does not share `irq`'s door).
  pit_channel(machine& box, pic::controller* irq) noexcept
      : box_(&box), irq_(irq) {}

  /// Power-on: no mode programmed, no deadline armed, gate high (the
  /// permanently-high state channels 0 and 1 are wired to and channel 2
  /// starts in until something drives its gate low).
  void reset() noexcept;

  /// This channel's state, in a fixed order (state.h). The deadline it
  /// has armed is the scheduler's to write, not this channel's.
  void save_state(state_sink& out) const;

  /// 43h decoded down to "this channel, this access mode, this mode" —
  /// `pit::write_port` is where SC/RW/M/BCD get pulled apart and
  /// validated; by the time this is called, `access` and `mode` are
  /// already known-good.
  void write_control(pit_access access, pit_mode mode) noexcept;

  /// 43h's RW=00: snapshot the live count for `read_data()` to return
  /// until it has been fully read (this file's top comment).
  void latch() noexcept;

  /// This channel's data port, read.
  [[nodiscard]] std::uint8_t read_data() noexcept;

  /// This channel's data port, written.
  void write_data(std::uint8_t byte) noexcept;

  /// The external GATE input. Only `pit::set_gate2()` ever calls this —
  /// channels 0 and 1 are tied high in every machine this class is used
  /// to build and never see a call.
  void set_gate(bool level) noexcept;

  /// Channel 2's OUT line at tick `at`, and the next tick it will change
  /// on its own. See this file's top comment, "Channel 2's OUT line."
  [[nodiscard]] bool output(ticks at) const noexcept;
  [[nodiscard]] ticks next_output_change(ticks at) const noexcept;

  /// Only ever called by `pit::set_channel2_observer()` — see
  /// `pit_channel_observer` above.
  void set_observer(pit_channel_observer& observer) noexcept {
    observer_ = &observer;
  }

  void on_deadline(ticks due) override;

 private:
  /// `write_control()`, `write_data()` and `set_gate()`'s shared exit
  /// notification (this file's top comment). A no-op for channels 0 and
  /// 1, whose `observer_` is never set.
  void notify_observer() const noexcept {
    if (observer_ != nullptr) {
      observer_->on_channel2_changed(now());
    }
  }

  /// The cycle `output()`/`next_output_change()` are really answering
  /// about as of `at`: `active_since_`/`active_divisor_`, folding in the
  /// one pending reload if its boundary has already passed `at` — the
  /// same lookahead `catch_up()` performs, restated as a pure query so a
  /// level or a next-change tick can be answered without mutating
  /// anything (a caller may ask about a tick more than once, and about
  /// ticks it has already passed).
  struct effective_cycle {
    ticks since;
    std::uint32_t divisor;
  };
  [[nodiscard]] effective_cycle cycle_at(ticks at) const noexcept;

  [[nodiscard]] ticks now() const noexcept;

  /// The 0-means-65536 rule (clock.h's sibling fact for the PIT: a
  /// 16-bit register cannot spell its own top value), applied at the one
  /// place a raw register value becomes a divisor.
  [[nodiscard]] static std::uint32_t divisor_of(std::uint16_t reload) noexcept {
    return reload == 0 ? 0x10000u : reload;
  }

  /// `now`, or the tick the gate last went low if it is currently low —
  /// the one place "what tick is this channel's arithmetic actually
  /// standing on" is decided, so `catch_up()` and `live_count()` can
  /// never disagree about it.
  [[nodiscard]] ticks reference_tick(ticks at) const noexcept;

  /// Apply the single pending reload (modes 2/3 only) if its boundary
  /// has passed `at` — called from every path whose answer could depend
  /// on it (this file's top comment), so no caller has to reason about
  /// whether an earlier one already ran.
  void catch_up(ticks at) noexcept;

  /// Begin a fresh cycle at `at`, counting from `divisor`: the shared
  /// tail of an initial load, a mode-0 reload, a modes-2/3 boundary (via
  /// `catch_up()` or `on_deadline()`), and a modes-2/3 gate rising edge.
  /// Arms this channel's next deadline if it is gated on; disarms it
  /// (and leaves the state exactly as loaded) if it is not, since
  /// nothing will happen to it until the gate says otherwise.
  void start_cycle(ticks at, std::uint32_t divisor) noexcept;

  /// The count `read_data()`/`latch()` answer: pure, and does not by
  /// itself account for a pending reload whose boundary has passed —
  /// callers go through `catch_up()` first.
  [[nodiscard]] std::uint32_t live_count(ticks at) const noexcept;

  machine* box_;
  pic::controller* irq_;
  /// Only ever non-null for channel 2 (`pit::set_channel2_observer()`).
  pit_channel_observer* observer_{nullptr};

  pit_mode mode_{pit_mode::none};
  pit_access access_{pit_access::none};

  bool awaiting_msb_{false};
  std::uint16_t partial_write_{0};
  bool next_read_is_msb_{false};

  /// 0 means "unprogrammed" — no control word has completed a load yet.
  std::uint32_t active_divisor_{0};
  ticks active_since_{0};

  bool has_pending_{false};
  std::uint32_t pending_divisor_{0};

  bool gate_{true};
  ticks gate_paused_at_{0};

  bool latched_{false};
  std::uint16_t latched_value_{0};
};

/// The device at 40h-43h: three `pit_channel`s and the control-word
/// decoder in front of them.
class pit final : public device {
 public:
  /// `box` must outlive this, and so must `irq0` — the controller
  /// channel 0's output edge reaches (pic.h). Ordinary machine wiring:
  /// `pit thepit(box, thepic); box.attach(thepit);
  /// box.schedule(thepit.channel0_deadline());
  /// box.schedule(thepit.channel2_deadline());` — two schedule() calls
  /// and not three, for the reason this file's top comment gives.
  pit(machine& box, pic::controller& irq0) noexcept;

  static constexpr port_range port_window{.first = pit_channel0_port,
                                          .last = pit_control_port};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&port_window, 1)};
  }

  void reset() override;
  void save_state(state_sink& out) const override;

  [[nodiscard]] std::uint8_t read_port(std::uint16_t port) override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  /// The channel-2 gate line (this file's top comment); M2-D4/#49's port
  /// 61h bit 0 drives it.
  void set_gate2(bool level) noexcept { channel2_.set_gate(level); }

  /// Channel 2's OUT line, and when it will next change on its own — the
  /// speaker's whole read-side interface to this device (this file's top
  /// comment, "Channel 2's OUT line"). Neither is `channel2_`'s own
  /// business past this point; nothing outside `pit_channel` ever touches
  /// its private state directly.
  [[nodiscard]] bool channel2_output(ticks at) const noexcept {
    return channel2_.output(at);
  }
  [[nodiscard]] ticks channel2_next_output_change(ticks at) const noexcept {
    return channel2_.next_output_change(at);
  }

  /// The one listener a reprogram of channel 2 notifies — see
  /// `pit_channel_observer`. M2-D4/#49's speaker is the only caller this
  /// machine has.
  void set_channel2_observer(pit_channel_observer& observer) noexcept {
    channel2_.set_observer(observer);
  }

  /// The scheduler participants channels 0 and 2 are. Registering them
  /// is the wiring code's job, the same way attaching this device for
  /// the bus is (`machine::schedule`, `machine::attach`).
  [[nodiscard]] scheduled& channel0_deadline() noexcept { return channel0_; }
  [[nodiscard]] scheduled& channel2_deadline() noexcept { return channel2_; }

 private:
  /// 43h: pull SC/RW/M/BCD apart, validate them, and either latch or
  /// program the addressed channel.
  void write_control(std::uint8_t value);

  pit_channel channel0_;
  pit_channel channel1_;
  pit_channel channel2_;
};

}  // namespace amberfolio::machine
