// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine: RAM, the two maps, the devices attached to them, and the
// processor executing against the lot.
//
// cpu/bus.h has been promising this since M1 — "the machine implements it
// over real RAM and the device map (M2). The CPU never knows which" — so
// the machine *is* the bus rather than owning one. There is no adapter in
// between and no second object to keep in step: a bus cycle arrives at
// `read_memory` here, gets classified by the memory map, and goes to RAM,
// to a device, or nowhere.
//
// The machine also owns the virtual clock, because time is machine state
// and nothing else in the tree is allowed to have any (clock.h). A step
// costs a fixed number of ticks, the scheduler wakes whatever is due at
// the boundary before each step, and `run()` is the loop over the two.
// Nothing here reads the host's clock, ever: that is what makes a run
// replayable (PLAN.md §4).
//
// The machine is also where the narrow platform interface lives
// (platform.h): the framebuffer a host presents, the audio timeline it
// pulls, the key events it pushes, the wall clock it seeds, and the
// console bytes it drains. They are members rather than a separate
// object, because each of them is either machine state or a buffer whose
// lifetime is exactly this one's. Read platform.h's top comment before
// writing a host; it is the design document for M2-H1 (#54) and M2-H2
// (#55).
//
// What is deliberately not here yet:
//
//   * **The services.** The BIOS is here — the vector table, the callout
//     stubs and the BDA, all of it real memory in the region the map
//     reserves (service_floor.h) — but the only body behind it is the
//     default timer tick. INT 21h is M2-D7, the keyboard is M2-D8, INT
//     10h is M2-D3, and each of them is a handler installed into the
//     floor rather than a change to this file.
//   * **Devices.** Every one of them is an M2-D issue. This layer knows
//     the contract (device.h) and nothing about any implementation.
//
// A machine has a megabyte of RAM inside it, so it is an object to put on
// the heap, not on a stack. Every user of it holds one for the length of
// a run, which makes that a one-line cost.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/keyboard.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/port_map.h"
#include "amberfolio/machine/scheduler.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/trace.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

/// What one call to `machine::run()` did.
///
/// Why it came back is not in here, because there are exactly two
/// reasons and one of them is already recorded, sticky and inspectable:
/// either virtual time reached `until`, or the machine stopped and
/// `stopped()` says so. Restating that as a third field would only be
/// able to disagree with it — the same discipline `attach()` follows.
struct run_result {
  /// Virtual time the run consumed. `machine::time()` is where it left
  /// the clock; this is the difference, which is the number a caller
  /// pacing itself against wall time actually wants.
  ticks elapsed{};

  /// Scheduling steps taken. Steps and elapsed ticks are the same fact
  /// twice while the governor is left alone — `elapsed == steps *
  /// step_cost()` — and it is the pair that makes a step-cost test able
  /// to say which of the two went wrong.
  std::uint64_t steps{};
};

class machine final : public cpu::bus {
 public:
  /// How many devices can be attached. M2 has four in the plan — PIT,
  /// PIC, EGA, speaker — and eight leaves room for the milestone to
  /// surprise us without making `reset()` a data structure.
  static constexpr std::size_t max_devices = 8;

  /// `log` may be null, and everything the machine does is the same
  /// either way: the first-touch bookkeeping behind the notices runs
  /// whether or not anything is listening, so a run with a sink attached
  /// and a run without one are the same run. (cpu/diagnostics.h makes the
  /// same promise one level down, for the same reason.)
  explicit machine(memory_layout layout = memory_layout::pc,
                   diagnostics* log = nullptr);

  /// Give `dev` the memory windows and the ports it claims, and put it on
  /// the list `reset()` walks.
  ///
  /// False, and the machine stopped with `stop_reason::conflicting_claim`,
  /// if any of that collides with a device already attached or there is
  /// no room left. The collision is reported and the machine is left
  /// stopped, so a caller that ignores the answer still finds out — the
  /// same discipline the processor's own stops follow.
  bool attach(device& dev);

  /// Register `who` with the scheduler, so that it can post deadlines and
  /// be woken at them (scheduler.h).
  ///
  /// A second, separate thing to be — device.h said it would arrive this
  /// way and not as another virtual on `device`, and the two halves are
  /// independent: most devices are never scheduled, the renderer and the
  /// audio mixer will be scheduled and answer no bus cycles at all, and
  /// the PIT will be both, attached and scheduled.
  ///
  /// Its own name rather than a second `attach()` overload, precisely
  /// because of that last case: a class deriving from both `device` and
  /// `scheduled` converts to each base equally well, so `attach(pit)`
  /// would be ambiguous and every caller would have to cast. Two names
  /// for two wirings costs nothing and reads as what it is —
  /// `pc.attach(pit); pc.schedule(pit);`
  ///
  /// Registration order is the scheduler's tie-break, so this is also
  /// where "which of two devices due on the same tick goes first" is
  /// decided: it is decided by how the machine is wired up.
  ///
  /// False, and the machine stopped with `stop_reason::conflicting_claim`,
  /// if there is no room left or `who` is already registered — the same
  /// answer and the same discipline `attach()` gives, because it is the
  /// same kind of mistake.
  bool schedule(scheduled& who);

  /// The RESET line: the processor and every attached device go to
  /// power-on state, the recorded stop is cleared, the virtual clock goes
  /// back to zero with every deadline disarmed, the BIOS lays its vector
  /// table, stubs and data area back down, the maps start noticing
  /// untouched pages and ports again, and every device's fault clears
  /// with everything else (device.h's `clear_fault()`, #65) — a fault is
  /// a device's own bus-cycle state, not part of what "attached" means,
  /// the same distinction this line already draws for what it claimed.
  ///
  /// Memory keeps what it held. That is what the line does — RESET on a
  /// PC does not clear RAM, which is how a warm boot can be told from a
  /// cold one — and a machine that must start from nothing is
  /// constructed, not reset. Attached devices stay attached: what they
  /// claimed is how the machine is wired, not part of its state. So does
  /// the speed governor, for the same reason — it is a setting, not
  /// something the machine arrived at.
  ///
  /// The clock does go back to zero, and every deadline with it. RESET on
  /// a real PC has no clock to zero, but this one is the time base a run
  /// is recorded against, and a replay wants the run to start at tick 0
  /// at the same moment every time. A deadline is something a device
  /// posted while running, so none of them can survive the line either;
  /// a device that wants one from power-on arms it in its own `reset()`,
  /// which this calls.
  void reset();

  /// One scheduling step: the deadlines due at this moment, then one step
  /// of the processor — one instruction, one iteration of a repeated
  /// string instruction, or one interrupt delivery (cpu::step_status) —
  /// then `step_cost()` ticks on the virtual clock.
  ///
  /// Deadlines first, and at a step boundary, because that boundary is
  /// also where the processor recognizes interrupts: a device that raises
  /// a line from `on_deadline` has it seen by the instruction this very
  /// call goes on to run, and no device ever observes the CPU partway
  /// through one.
  ///
  /// Then, still before an instruction is fetched, the machine compares
  /// CS against the segment the BIOS callout stubs live in, and when it
  /// matches runs the native handler the program's interrupt reached
  /// (service_floor.h). That comparison is the entire cost of the service
  /// floor on a step that is not a service call.
  ///
  /// That order is not arbitrary. A deadline handler may raise a line,
  /// and the callout defers to an interrupt that is due precisely so a
  /// handler and its stub's IRET are never split by one. Waking devices
  /// first is what puts the line up before the callout asks.
  ///
  /// The clock advances by the same amount whatever the step did,
  /// including a `halted` one where the processor consumed nothing. That
  /// is the point of a halted machine: it is burning time waiting for an
  /// interrupt, and the only thing that can produce one is a deadline
  /// arriving, which needs the clock to keep moving. (A machine that
  /// spent whole seconds halted could skip the clock straight to
  /// `deadlines().next_deadline()` instead of stepping through it; that
  /// is an optimization with the same observable behaviour, and it is not
  /// worth writing before a profile asks for it.)
  ///
  /// A stopped machine keeps answering `stopped`, touches neither the bus
  /// nor the devices, and costs no time — so a caller that does not check
  /// can loop harmlessly rather than execute past the thing it was told
  /// about, and the clock does not run away while it does.
  cpu::step_status step();

  /// Step until the virtual clock reaches `until`, or until the machine
  /// stops.
  ///
  /// `until` is an absolute tick, not a duration: a caller pacing a frame
  /// says `run(pc.time() + ticks_per_frame)` and, better, a caller pacing
  /// a whole run keeps its own schedule and passes points on it, which is
  /// what keeps the pacing from drifting.
  ///
  /// The clock may end up slightly *past* `until` — by less than one step
  /// cost — because a step is indivisible and stopping short of `until`
  /// would mean a `run()` that never advanced at all whenever the
  /// remaining time was smaller than a step. The overshoot costs nothing:
  /// a deadline still fires at exactly the tick it was armed for, and the
  /// caller's next `until` is a point on its own schedule rather than
  /// this one, so the overshoot does not accumulate.
  ///
  /// A machine that stops midway returns early with what it did up to
  /// that point, and leaves the reason in `stop()` — the stop is sticky
  /// and inspectable, so `run()` does not restate it (see `run_result`).
  /// Calling `run()` on an already-stopped machine takes no steps and
  /// costs no time.
  run_result run(ticks until);

  /// The virtual clock: ticks of the PIT input clock since the last
  /// reset. All machine-visible time is this (clock.h).
  [[nodiscard]] ticks time() const noexcept { return now_; }

  /// Scheduling steps taken since the last reset — the other axis a run
  /// is measured on, and the one a boot report is read against.
  ///
  /// Ticks and steps are the same fact twice while the speed governor is
  /// left alone (`run_result`), and they stop being so the moment it is
  /// not: a run that changed speed halfway has a tick count that no
  /// longer divides. The step count is what stays comparable, which is
  /// why "the same stop line at the same step" is the claim M3's two
  /// hosts make to each other (#84) rather than "at the same tick".
  ///
  /// Kept here rather than accumulated by each caller out of
  /// `run_result::steps`, because a stop report has to be able to say
  /// where a run got to without depending on the caller having counted
  /// — and because the two hosts would otherwise each keep their own,
  /// which is two chances to disagree about a number they exist to
  /// agree on.
  [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }

  /// What one step costs, in ticks. The speed governor is this one
  /// number, and the presets are names for values of it.
  [[nodiscard]] ticks step_cost() const noexcept { return step_cost_; }

  /// Put the governor on a named preset (clock.h).
  void set_speed(speed_preset preset) noexcept {
    step_cost_ = ticks_per_step(preset);
  }

  /// Set the step cost directly, for a calibration run or a test that
  /// wants round numbers. False, and nothing changed, for zero: a step
  /// that costs no time is a machine whose clock never moves, whose
  /// deadlines therefore never arrive, and whose `run()` would never
  /// return.
  ///
  /// There is no accessor for "which preset is set", because after this
  /// call there might not be one. The step cost is the state; a preset is
  /// a way of writing a value of it down.
  bool set_step_cost(ticks cost) noexcept;

  /// The deadline queue. `pc.deadlines().arm(dev, when)` is how a device
  /// posts its next moment; `schedule()` is how it earns the
  /// right to.
  [[nodiscard]] scheduler& deadlines() noexcept { return deadlines_; }
  [[nodiscard]] const scheduler& deadlines() const noexcept {
    return deadlines_;
  }

  /// The processor. Spelled `processor()` and not `cpu()` because
  /// `cpu::processor` is the type: a member named `cpu` would hide the
  /// namespace inside this class and make the type unnameable.
  [[nodiscard]] cpu::processor& processor() noexcept { return cpu_; }
  [[nodiscard]] const cpu::processor& processor() const noexcept {
    return cpu_;
  }

  /// The address space. `memory().ram()` is how the machine's own
  /// writers — a loader, the BIOS setup, a test — put bytes down without
  /// going through a bus cycle (memory_map.h).
  [[nodiscard]] memory_map& memory() noexcept { return memory_; }
  [[nodiscard]] const memory_map& memory() const noexcept { return memory_; }

  [[nodiscard]] const port_map& ports() const noexcept { return ports_; }

  /// The BIOS/DOS service floor: where a service layer installs its
  /// handlers, and where the vector table and the BDA came from.
  [[nodiscard]] service_floor& services() noexcept { return services_; }

  /// The trace ring: the last N instructions and service calls, kept
  /// only when a caller has asked for them (trace.h, M3-F1 #83).
  ///
  /// A member of the machine rather than something a host bolts on,
  /// because the two things worth recording are seen nowhere else: CS:IP
  /// at a step boundary is `step()`'s alone, and a service call is built
  /// inside the floor. A host outside the machine could observe neither
  /// without the machine handing it over one at a time, which is a
  /// callback per instruction for a facility that is off by default.
  [[nodiscard]] trace_ring& trace() noexcept { return trace_; }
  [[nodiscard]] const trace_ring& trace() const noexcept { return trace_; }

  /// The most recent call into the BIOS/DOS layer, or null if the
  /// program has not made one since the last reset.
  ///
  /// Kept unconditionally, unlike the trace ring: it is eight bytes, it
  /// is built on every call anyway (service_floor.cpp), and it is the
  /// half of a stop report that turns "reason 2 at 0B5D2" into "INT 21h
  /// AH=48h from 1A2B:00C6" — which is the difference between a stop and
  /// a worklist line (#81, #83). A report that had it only when tracing
  /// was on would be a report nobody could rely on.
  [[nodiscard]] const service_call* last_service_call() const noexcept {
    return have_service_call_ ? &last_service_call_ : nullptr;
  }

  /// The most recent device refusal (device.h's `device_fault`, enriched
  /// by `note_device_fault()` with where the program was), or null if no
  /// device has refused anything since the last reset. Kept for the same
  /// reason and at the same cost as `last_service_call()`.
  [[nodiscard]] const device_stop* last_device_stop() const noexcept {
    return have_device_stop_ ? &last_device_stop_ : nullptr;
  }

  /// The service floor's way of telling the machine what it just built
  /// (service_floor.cpp). Not something a handler or a host calls: it is
  /// one link of the machine's own internal wiring, public only because
  /// `service_floor` is a separate class rather than a friend — the same
  /// shape `stop_unimplemented_service()` below already has.
  void note_service_call(const service_call& call) noexcept;

  /// A service handler's own "unimplemented," one level finer than the
  /// floor's: INT 16h (keyboard.h, M2-D8) and INT 21h (M2-D7) each answer
  /// for one vector but dispatch several functions inside it by AH,
  /// where `service_floor`'s null-handler check (service_floor.h) cannot
  /// see the difference between a vector with nothing behind it and a
  /// function an installed handler does not recognize. Same stop
  /// (`stop_reason::unimplemented_service`), same discipline (PLAN.md
  /// §3) — `at` is the physical address of the stub the call reached,
  /// the same value `dispatch_services()` would have used had the floor
  /// caught this itself.
  bool stop_unimplemented_service(std::uint32_t at);
  /// The seam engine (seam.h, PLAN.md §5): the one mechanism by which
  /// anything other than the program's own instructions may touch this
  /// machine, and off by default. A host enables a seam through it; a
  /// seam handler reaches back through it for `image_base()`.
  [[nodiscard]] seam_engine& seams() noexcept { return seams_; }
  [[nodiscard]] const seam_engine& seams() const noexcept { return seams_; }

  /// The DOS handle table and exit state INT 21h's handlers use
  /// (dos.h, M2-D7, #52) — present whether or not a program ever calls
  /// INT 21h, the same as the platform-interface members below.
  [[nodiscard]] dos_services& dos() noexcept { return dos_; }
  [[nodiscard]] const dos_services& dos() const noexcept { return dos_; }

  /// The filesystem INT 21h's file functions operate over. Null until a
  /// host or a test calls `set_filesystem()` — a machine with no
  /// filesystem attached is a real, testable state (a program that never
  /// touches a file behaves identically either way), not an error.
  [[nodiscard]] filesystem* vfs() noexcept { return vfs_; }
  [[nodiscard]] const filesystem* vfs() const noexcept { return vfs_; }

  /// Attach `fs`, which must outlive this. Nullable and settable rather
  /// than a constructor argument because a test builds a `machine` and a
  /// `memory_filesystem` from two different places (the latter heap
  /// allocated — memory_vfs.h says why) and wants to attach it once both
  /// exist, the same shape `attach(device&)` already has.
  void set_filesystem(filesystem& fs) noexcept { vfs_ = &fs; }

  // --- The platform interface -----------------------------------------
  //
  // The five things a host talks to, and the whole of what crosses the
  // core/host boundary (platform.h — read its top comment before writing
  // a host). They are members of the machine and not a separate object a
  // host has to be handed, because every one of them is either machine
  // state or a buffer whose lifetime is the machine's, and a host that
  // has a `machine&` should not need a second reference to do anything.
  //
  // Everything here is machine-thread only, with exactly one exception:
  // `audio().render()`, which is the audio thread's and is the reason
  // platform.h has a threading contract at all.

  /// The completed frame the host presents, and the generation counter
  /// that says whether it is a new one. The renderer (M2-D3, #48) is the
  /// writer.
  [[nodiscard]] framebuffer& display() noexcept { return display_; }
  [[nodiscard]] const framebuffer& display() const noexcept { return display_; }

  /// The speaker's edge list, and the pull that turns it into samples.
  /// The speaker (M2-D4, #49) publishes into it; `run()` publishes the
  /// horizon.
  [[nodiscard]] audio_timeline& audio() noexcept { return audio_; }
  /// The const view is the counters and nothing else — `render()` moves
  /// the consumer's cursor and so cannot be const, which is exactly the
  /// distinction worth having here: a host reporting underruns is not
  /// pulling audio.
  [[nodiscard]] const audio_timeline& audio() const noexcept { return audio_; }

  /// Key events waiting for the keyboard service (M2-D8, #53) to drain
  /// them. Use `post_key()` to put one in — see below.
  [[nodiscard]] input_queue& input() noexcept { return input_; }

  /// The date and time DOS 2Ah/2Ch report (M2-D7, #52). Seeded through
  /// `set_wall_time()`; read with `wall().at(time())`.
  [[nodiscard]] wall_clock& wall() noexcept { return wall_; }
  [[nodiscard]] const wall_clock& wall() const noexcept { return wall_; }

  /// DOS console output, as bytes the host drains. There is no text-mode
  /// video in this machine and none is planned.
  [[nodiscard]] console_output& console() noexcept { return console_; }
  [[nodiscard]] const console_output& console() const noexcept {
    return console_;
  }

  /// Inject a key event at the machine's current position in virtual
  /// time, which is the tick `time()` is standing on.
  ///
  /// On the machine and not on `input_queue` because the timestamp is the
  /// determinism guarantee: it has to be the machine's own clock, and the
  /// only object that can say what that reads is this one. A host calling
  /// `input().post(...)` with a tick of its own choosing would be forging
  /// the one field a replay depends on.
  ///
  /// False if the queue is full; `input().dropped()` counts it.
  bool post_key(std::uint8_t scancode, key_action action) noexcept {
    return input_.post(scancode, action, now_);
  }

  /// Seed the wall clock: at this moment in virtual time, the date and
  /// time out in the world are `when`. Every later 2Ah/2Ch read is this
  /// instant plus the virtual time since (platform.h).
  ///
  /// False, and nothing changed, if `when` is not a real date and time.
  bool set_wall_time(const wall_time& when) noexcept {
    return wall_.set(when, now_);
  }

  /// True once the machine has stopped, for its own reason or because the
  /// processor did. Sticky until `reset()`.
  [[nodiscard]] bool stopped() const noexcept {
    return stop_.reason != stop_reason::none;
  }

  [[nodiscard]] const stop_record& stop() const noexcept { return stop_; }

  /// The program terminated itself. Records
  /// `stop_reason::program_exited` with `code` and tells the sink, the
  /// same way any other stop does; `stopped()` is true from here on and
  /// `step()`/`run()` go inert exactly as they do for any other stop.
  ///
  /// The PSP's INT 20h (M2-D6, #51 — always code 0, see diagnostics.h)
  /// and INT 21h AH=4Ch (M2-D7, #52 — AL) both call this rather than
  /// each recording their own stop, so a program that exits either way
  /// is reported identically and a caller watching for the end of a run
  /// has exactly one thing to check. Public, not something only the
  /// service floor reaches, because #52's handler is not written by this
  /// issue and needs a stable entry point to call into (its own header's
  /// coordination note says so).
  void exit_program(std::uint8_t code);
  // --- Video mode discipline -------------------------------------------
  //
  // Bookkeeping for the video BIOS (INT 10h, M2-D3, #48): has AH=00h
  // programmed a mode yet? It lives here, on the machine, rather than on
  // the EGA device that answers the video window, for the same reason a
  // native handler reaches `input()`, `wall()` and `console()` through
  // the machine instead of through a device pointer of its own:
  // `service_handler` is a plain function pointer with nowhere to keep
  // one (service_floor.h), so a handler's only way at machine state is
  // what `machine` itself hands out. Attached devices are held
  // generically (`device&`, never a concrete type — this file's own
  // "what is deliberately not here yet" note above), so there is no
  // `ega&` to be given even if the pointer type were not the problem.
  //
  // The flag gates nothing in the write pipeline — a plane takes a byte
  // the same way whether or not a mode has been set, which is the true
  // hardware answer (device.h, ega.h). What it gates is the notice
  // `write_memory` reports: a program that writes into the video window
  // before AH=00h has run is not stopped, but PLAN.md §3 wants it said
  // once rather than silently accommodated.

  /// AH=00h calls this once it has programmed mode 0Dh. Nothing else ever
  /// sets it; `reset()` clears it, because a reset video card has
  /// forgotten its mode exactly as it forgot every register the EGA
  /// itself owns.
  void note_video_mode_set() noexcept { video_mode_set_ = true; }

  [[nodiscard]] bool video_mode_set() const noexcept { return video_mode_set_; }

  /// The video BIOS programmed a mode this machine cannot display
  /// (diagnostics.h's `undisplayable_video_mode`, int10.h, #87). Reported
  /// once per distinct mode number, on the same first-touch rule notices
  /// about memory and ports already follow: a program that flips between
  /// two modes should produce two lines and not two thousand.
  ///
  /// Public for the reason `stop_unsupported_request()` is: a handler
  /// calls it from outside this class.
  void notice_video_mode(std::uint8_t mode);

  /// A native service handler's own refusal: it understood the request
  /// and does not support it — an INT 10h video mode this machine does
  /// not have, say. PLAN.md §3's "loud log line and a clean stop," at the
  /// granularity of one call rather than one vector, which is what
  /// `stop_reason::unimplemented_service` already covers for a vector
  /// with no handler at all. `at` is whatever the handler wants
  /// remembered about the call — often the caller's AX.
  ///
  /// Public, unlike `stop_with`, because a handler calls it from outside
  /// this class — `floor.box().stop_unsupported_request(...)` — the same
  /// way it reaches every other piece of machine state above.
  bool stop_unsupported_request(std::uint32_t at) {
    return stop_with(stop_reason::unsupported_request, at);
  }

  /// Stop because a service handler discovered, mid-body, that the
  /// particular sub-function it was asked to perform is not implemented —
  /// finer grain than `dispatch_services()`'s own detection, which only
  /// sees whole vectors. INT 21h is one vector serving many AH values
  /// (dos.h, M2-D7, #52), and an unbacked one has to refuse exactly as an
  /// unbacked vector would (PLAN.md §3) rather than silently returning.
  /// `at` is the caller's CS:IP as a physical address — where the log line
  /// this stop goes with points.
  void stop_unimplemented_function(std::uint32_t at) noexcept {
    stop_with(stop_reason::unimplemented_service, at);
  }

  // --- cpu::bus -------------------------------------------------------
  //
  // The routing, and the only place an address or a port becomes a
  // decision. Public because that is what the interface is; callers go
  // through the processor.

  [[nodiscard]] std::uint8_t read_memory(std::uint32_t address) override;
  void write_memory(std::uint32_t address, std::uint8_t value) override;
  [[nodiscard]] std::uint8_t read_port8(std::uint16_t port) override;
  void write_port8(std::uint16_t port, std::uint8_t value) override;

  // read_port16 / write_port16 are left as bus.h defines them: two byte
  // cycles, low half first. Nothing on this bus answers a word in one
  // transfer, and a machine that pretended otherwise would hide the
  // access pattern a 16-bit device would eventually have to override.

 private:
  /// The cold half of the step-boundary service check: CS is already the
  /// stub segment, so work out whether this is really a stub and run what
  /// is behind it. Out of line and out of `step()` because none of it is
  /// on the hot path — reaching it means the program called the BIOS.
  void dispatch_services();

  /// Record a stop, tell the sink once, and answer false so that
  /// `attach()` can `return` it.
  bool stop_with(stop_reason reason, std::uint32_t at);

  /// Report `what`, if this is the first time anything has been asked of
  /// that page or that port since the last reset. Fills in where the
  /// program was; the caller supplies the rest.
  void notice_memory(notice_kind what, std::uint32_t address,
                     std::uint8_t value);
  void notice_port(notice_kind what, std::uint16_t port, std::uint8_t value);

  /// Checked right after every dispatch to `dev` (device.h, #65): if it
  /// just faulted, turn that into a real stop and a `device_stop` report,
  /// exactly as `notice_memory`/`notice_port` turn a touch of nothing
  /// into a `notice`. A no-op — one `bool` read — the rest of the time,
  /// which is what makes it cheap enough to call unconditionally rather
  /// than only where a device happens to be known to refuse things.
  void note_device_fault(device& dev);

  memory_map memory_;
  port_map ports_;
  diagnostics* log_;

  std::array<device*, max_devices> devices_{};
  std::size_t attached_{};

  scheduler deadlines_;

  /// The virtual clock, and the one number the speed governor is. Plain
  /// members rather than a `clock` object: a class here would be one
  /// integer with a getter and an adder, and the only code allowed to
  /// move it is in this file anyway.
  ticks now_{};
  ticks step_cost_{ticks_per_step(default_speed)};

  /// Steps since the last reset — see `steps()`. Beside the clock
  /// because it is the same kind of thing: a count the machine keeps of
  /// its own running, moved by nothing but `step()`.
  std::uint64_t steps_{};

  cpu::processor cpu_;
  service_floor services_;

  /// INT 16h, the BDA keystroke buffer, and Ctrl-Break (keyboard.h,
  /// M2-D8). A member and not wiring installed from outside, unlike a
  /// device: it is the machine's own BIOS layer, always present, exactly
  /// as `services_` is.
  keyboard_service keyboard_;

  stop_record stop_{};

  /// The trace ring, and the two single-entry records that are always
  /// kept — see `trace()` and `last_service_call()` above for why the
  /// three are not one thing.
  trace_ring trace_;
  service_call last_service_call_{};
  device_stop last_device_stop_{};
  bool have_service_call_{false};
  bool have_device_stop_{false};

  /// The video BIOS's own bookkeeping — see "Video mode discipline"
  /// above.
  bool video_mode_set_{false};
  /// The DOS layer's own state (dos.h, M2-D7, #52) and the filesystem its
  /// file functions reach through `vfs()` — host- or test-attached, null
  /// until then.
  dos_services dos_;
  filesystem* vfs_{};

  /// Enabled seams and their armed interception points (seam.h). Costs
  /// `step()` one `bool` test when nothing is on, which is always unless
  /// somebody asked otherwise.
  seam_engine seams_;

  /// The platform interface (platform.h). Members rather than something
  /// a host supplies, because the buffers have to outlive every pull and
  /// the machine is the only thing that knows how long that is.
  framebuffer display_;
  audio_timeline audio_;
  input_queue input_;
  wall_clock wall_;
  console_output console_;

  /// How much of the address space one notice speaks for. Fine enough
  /// that two different absent things do not share a line, coarse enough
  /// that a run over one region is one line.
  static constexpr std::uint32_t notice_page_size = 4096;

  /// One bit per page, and one per port: what has already been noticed.
  /// Both tables together are eight kilobytes beside the megabyte they
  /// are about.
  std::array<std::uint64_t, cpu::address_space_size / notice_page_size / 64>
      pages_noticed_{};
  std::array<std::uint64_t, 65536 / 64> ports_noticed_{};

  /// One bit per video mode number: which have already been reported as
  /// undisplayable (`notice_video_mode`). Thirty-two bytes, cleared by
  /// `reset()` with the other two.
  std::array<std::uint64_t, 256 / 64> video_modes_noticed_{};
};

}  // namespace amberfolio::machine
