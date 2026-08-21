// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/machine.h"

#include <limits>
#include <span>

namespace amberfolio::machine {
namespace {

/// Set bit `index` in a bitmap and answer whether it was clear before —
/// "is this the first time?", asked of pages and of ports.
bool first_touch(std::span<std::uint64_t> bits, std::uint32_t index) {
  const std::uint64_t bit = std::uint64_t{1} << (index % 64u);
  std::uint64_t& word = bits[index / 64u];
  if ((word & bit) != 0) {
    return false;
  }
  word |= bit;
  return true;
}

}  // namespace

machine::machine(memory_layout layout, diagnostics* log)
    : memory_(layout), log_(log), cpu_(*this), services_(*this, log) {
  // Power-on, and the self test with it: the vector table, the callout
  // stubs and the BDA are memory, and memory does not lay itself out.
  // In the constructor body rather than in the service floor's own
  // constructor, because that one runs while this object is still being
  // built and has no business reading it.
  services_.reset();

  // INT 16h and the default Ctrl-Break hook, installed into the floor
  // `reset()` just laid the stubs down for. Wiring, like the timer
  // handlers the floor installs itself — see keyboard.h.
  keyboard_.install(services_);
}

bool machine::attach(device& dev) {
  if (attached_ == max_devices) {
    return stop_with(stop_reason::conflicting_claim, 0);
  }

  // Claims are taken one at a time and a rejected one stops the machine
  // with the address or port it collided on, so the earlier claims of a
  // device that is refused halfway stay registered. That is deliberate:
  // the machine is stopped and is not going to run, and leaving the map
  // exactly as the failure found it is worth more to whoever reads the
  // stop than a tidy rollback would be.
  const claims wanted = dev.claimed();
  for (const memory_window window : wanted.memory) {
    if (!memory_.claim(window, dev)) {
      return stop_with(stop_reason::conflicting_claim, window.first);
    }
  }
  for (const port_range range : wanted.ports) {
    if (!ports_.claim(range, dev)) {
      return stop_with(stop_reason::conflicting_claim, range.first);
    }
  }

  devices_[attached_] = &dev;
  ++attached_;
  return true;
}

bool machine::schedule(scheduled& who) {
  if (!deadlines_.add(who)) {
    return stop_with(stop_reason::conflicting_claim, 0);
  }
  return true;
}

void machine::reset() {
  // The wall clock before the virtual one, because it is measured
  // against it: `rebase()` needs the tick the machine is leaving in order
  // to carry the instant across (platform.h). The date does not restart
  // — a wall clock does not — but the tick it is anchored to must.
  wall_.rebase(now_);

  // The clock first, so that a device arming a deadline from its own
  // reset() arms it against the time base the run is about to start on
  // rather than against the one that just ended.
  now_ = 0;
  subtick_ = 0;
  deadlines_.disarm_all();

  // The platform interface. The frame is blanked and republished so that
  // a host stops showing the previous run's last picture; the audio
  // timeline restarts at tick 0; the input queue and the console ring are
  // in-flight traffic belonging to the run that just ended, stamped
  // against a clock that no longer exists, so they go.
  display_.reset();
  audio_.restart();
  input_.clear();
  console_.clear();

  cpu_.reset();
  for (std::size_t i = 0; i < attached_; ++i) {
    devices_[i]->reset();
    devices_[i]->clear_fault();
  }

  // RAM survives the line, but the ROM code that runs after it does not
  // leave the low kilobyte as it found it: the vectors and the BDA are
  // written again, exactly as a real machine's self test writes them.
  // The handler table is untouched, because what a service layer
  // installed is wiring, like an attached device, and not state.
  services_.reset();

  // The keyboard's own auto-repeat bookkeeping (keyboard.h) — not BDA
  // memory, which `services_.reset()` just rewrote, but the private
  // state that decides whether a lock key's next press is a fresh one.
  keyboard_.reset();
  // The DOS handle table is the running program's state, not wiring —
  // unlike the handlers `install_dos_services()` installed, which
  // `services_.reset()` just proved it leaves alone. A warm boot gets the
  // same five standard handles a cold one does.
  dos_.reset();

  stop_ = {};
  // Filled in place rather than assigned an empty one, for the reason
  // `framebuffer::reset()` spells out: the port table is eight kilobytes
  // and an unoptimized build would put a copy of it on the stack to
  // clear it. `stop_` above is a handful of bytes and is left as it is.
  pages_noticed_.fill(0);
  ports_noticed_.fill(0);
  video_modes_noticed_.fill(0);

  // The run's own bookkeeping. `trace_.clear()` forgets what was
  // recorded and deliberately leaves `enabled()` alone: whether anything
  // is being recorded is a setting, like the speed governor above, and
  // not something the machine arrived at (trace.h).
  steps_ = 0;
  trace_.clear();
  have_service_call_ = false;
  have_device_stop_ = false;

  // Every seam off, and no program known: an enabled seam is a statement
  // about a particular binary that has been loaded (seam.h), and a reset
  // machine has not loaded one.
  seams_.clear();

  // The video BIOS's bookkeeping goes back to power-on state along with
  // everything else here: a reset machine has no mode set, exactly as a
  // freshly powered-on one does not.
  video_mode_set_ = false;
}

bool machine::set_step_cost(ticks cost) noexcept {
  if (cost == 0) {
    return false;
  }
  return set_step_cost_subticks(cost * subticks_per_tick);
}

ticks machine::time_after_steps(std::uint64_t steps) const noexcept {
  const ticks limit = std::numeric_limits<ticks>::max();
  if (step_cost_ != 0 && steps > (limit - subtick_) / step_cost_) {
    return limit;
  }
  const ticks by = (subtick_ + steps * step_cost_) / subticks_per_tick;
  return by > limit - now_ ? limit : now_ + by;
}

bool machine::set_step_cost_subticks(ticks cost) noexcept {
  if (cost == 0) {
    return false;
  }
  step_cost_ = cost;
  // The carried fraction belongs to the cost that produced it; keeping it
  // across a change would spend part of one machine's tick on another's.
  subtick_ = 0;
  return true;
}

cpu::step_status machine::step() {
  if (stopped()) {
    return cpu::step_status::stopped;
  }

  // Two things happen at this boundary, and the order is load-bearing.
  //
  // Deadlines first. A device woken here may raise a line — the PIT
  // raising IRQ0 is the whole reason the queue exists — and the callout
  // below has to be able to see that it did.
  deadlines_.dispatch_due(now_);

  // Then host key events, turned into BDA state before anything asks for
  // it: a program that reads 40:1E directly, and a blocking AH=00h
  // re-entering the INT 16h stub below, both need the buffer already
  // settled (keyboard.h's module comment has the whole argument). A
  // Ctrl-Break event may itself raise INT 1Bh here, which is why this
  // has to run before the CS check that follows — its own delivery can
  // be exactly what makes that check true.
  keyboard_.drain(*this);

  // Then the BIOS callout, and the whole of what it costs a step that is
  // not one: a single compare of CS against the segment the stubs live
  // in.
  //
  // Here — at the step boundary, before `cpu_.step()` and therefore
  // before any byte is fetched — because the native handler *is* the
  // service. The processor is about to execute the IRET that ends it, so
  // the body has to have run by now; and the boundary is also the only
  // place where CS is settled, which is what makes one comparison enough
  // to decide.
  //
  // It has to come second for the reason `dispatch_services()` defers to
  // a due interrupt at all. Run it first and a deadline could raise INTR
  // *after* the handler body has run but before its IRET: `cpu_.step()`
  // would then deliver the interrupt instead, pushing a return address
  // that points at the stub, and the IRET that eventually comes back
  // would arrive here at a boundary that runs the same handler a second
  // time. Dispatching deadlines first means any line they raise is
  // already up when the callout asks whether one is, so it defers, and
  // the interrupt's return lands on the stub at a boundary that is
  // finally clear.
  if (cpu_.regs()[cpu::sreg::cs] == service::stub_segment) {
    dispatch_services();
    if (stopped()) {
      return cpu::step_status::stopped;
    }
  }

  // Then the seams, if any are on (seam.h). Here rather than earlier for
  // the reason the callout is here: this is the only point at which CS:IP
  // is settled, and a handler that wants the instruction at it not to
  // happen has to run before it is fetched. After the callout, so that a
  // seam pointed at a BIOS stub sees the state the handler left rather
  // than the state that reached it.
  //
  // The cost when nothing is enabled is the `armed()` test alone.
  if (seams_.armed()) {
    seams_.dispatch(*this, cpu::physical_address(cpu_.regs()[cpu::sreg::cs],
                                                 cpu_.regs().ip));
  }

  // Last thing before the instruction, so that what is recorded is where
  // the processor actually stood when it ran one: after the deadlines,
  // after the keyboard drain, and after any service handler the callout
  // above ran. One branch when the ring is off (trace.h).
  trace_.record(
      trace_step{.cs = cpu_.regs()[cpu::sreg::cs], .ip = cpu_.regs().ip});

  const cpu::step_status status = cpu_.step();
  if (status != cpu::step_status::stopped) {
    ++steps_;
    // Charged for every status the processor can report, `halted`
    // included: a halted machine is waiting for an interrupt, and the
    // only thing that can bring one is a deadline, which needs the clock
    // to keep moving. A stop is the one thing that costs nothing —
    // nothing happened, and a caller looping past it must not be able to
    // run the clock away.
    // The clock, through the subtick accumulator (clock.h). On every
    // whole-tick preset `subtick_` is zero before and after, so this is
    // the plain `now_ += cost` it replaced; on a 386 it is what lets five
    // instructions share one tick.
    subtick_ += step_cost_;
    now_ += subtick_ / subticks_per_tick;
    subtick_ %= subticks_per_tick;
  } else {
    const cpu::stop_record& refused = cpu_.stop();
    stop_ = {.reason = stop_reason::processor,
             .at = cpu::physical_address(refused.cs, refused.ip)};
    // The processor's record, not ours: it names the opcode, and one stop
    // is one line (diagnostics.h). The test above is what keeps it one —
    // the processor's stop is sticky and would otherwise be re-reported
    // on every step a caller took past it.
    if (log_ != nullptr) {
      log_->report(refused);
    }
  }
  return status;
}

run_result machine::run(ticks until) {
  const ticks started = now_;
  run_result result{};

  // `<`, so a machine already at or past `until` does nothing at all and
  // the overshoot of one run does not turn into a stall in the next.
  while (now_ < until) {
    if (step() == cpu::step_status::stopped) {
      break;
    }
    ++result.steps;
  }

  // Publish the audio horizon: everything up to here is settled, so an
  // audio thread may integrate it (platform.h). Here rather than in
  // `step()` because this is the boundary a host runs to — a
  // single-stepping caller has no audio thread to serve, and paying an
  // atomic store per instruction for it would be a cost on the hot path
  // for nobody.
  audio_.advance(now_);

  result.elapsed = now_ - started;
  return result;
}

std::uint8_t machine::read_memory(std::uint32_t address) {
  switch (memory_.classify(address)) {
    case region::ram:
    case region::rom:
      return memory_.ram()[address];
    case region::device: {
      device& dev = *memory_.owner(address);
      const std::uint8_t value = dev.read_memory(address);
      note_device_fault(dev);
      return value;
    }
    case region::open_bus:
      break;
  }

  notice_memory(notice_kind::unmapped_memory_read, address, 0);
  return open_bus_value;
}

void machine::write_memory(std::uint32_t address, std::uint8_t value) {
  switch (memory_.classify(address)) {
    case region::ram:
      memory_.ram()[address] = value;
      return;
    case region::device: {
      // Mode discipline (see "Video mode discipline" in machine.h):
      // a write still lands whether or not AH=00h has run — the pipeline
      // does not know or care — but a program drawing into the video
      // window before it asked for a mode is running off-plan, and this
      // is the one place that can be noticed, before the write is
      // forwarded to whichever device answers the window.
      if (!video_mode_set_ && video_window.contains(address)) {
        notice_memory(notice_kind::video_write_before_mode_set, address, value);
      }
      device& dev = *memory_.owner(address);
      dev.write_memory(address, value);
      note_device_fault(dev);
      return;
    }
    case region::rom:
      notice_memory(notice_kind::rom_write, address, value);
      return;
    case region::open_bus:
      break;
  }

  notice_memory(notice_kind::unmapped_memory_write, address, value);
}

std::uint8_t machine::read_port8(std::uint16_t port) {
  if (device* dev = ports_.owner(port); dev != nullptr) {
    const std::uint8_t value = dev->read_port(port);
    note_device_fault(*dev);
    return value;
  }

  notice_port(notice_kind::unclaimed_port_read, port, 0);
  return open_bus_value;
}

void machine::write_port8(std::uint16_t port, std::uint8_t value) {
  if (device* dev = ports_.owner(port); dev != nullptr) {
    dev->write_port(port, value);
    note_device_fault(*dev);
    return;
  }

  notice_port(notice_kind::unclaimed_port_write, port, value);
}

void machine::dispatch_services() {
  if (!services_.enabled()) {
    // A flat machine has no BIOS region, so nothing was put at F000 and
    // a program executing there is executing its own code. See
    // service_floor::reset().
    return;
  }

  // Not at a boundary that owes an interrupt. The handler and the stub's
  // IRET are one thing: deliver an interrupt between them and the IRET
  // that eventually returns to the stub arrives at this test again and
  // runs the handler a second time. Deferring costs nothing — the
  // interrupt goes first and its return lands back on the stub at a
  // boundary that is finally clear. The reachable case is a program being
  // single-stepped over an INT, where the trap is owed by the INT
  // instruction itself and falls due inside the service it called.
  if (cpu_.interrupt_due()) {
    return;
  }

  // A loop rather than one dispatch, because a handler may hand control
  // to another stub — the default timer's chain into an unhooked INT 1Ch
  // does exactly that — and the second stub's handler has to run before
  // its IRET does. It ends three ways: the processor has left the
  // segment, it is somewhere in the segment that is not a stub, or the
  // dispatch left CS:IP where it found them, which is a handler that has
  // finished and now wants its IRET executed. The bound is not part of
  // that argument; it is what a cycle between two handlers costs instead
  // of hanging the emulator.
  for (unsigned pass = 0; pass < service::max_chain; ++pass) {
    if (cpu_.regs()[cpu::sreg::cs] != service::stub_segment) {
      return;
    }

    const std::uint16_t at = cpu_.regs().ip;
    const unsigned slot = service::stub_index(at);
    if (slot == service::not_a_stub) {
      // In the BIOS region but not at a stub. Nothing is faked and
      // nothing is refused: it is ROM, it reads back what is in it, and
      // the processor is welcome to execute it.
      return;
    }

    if (services_.call(slot) == service_outcome::unimplemented) {
      // The service floor reported which service and from where; this is
      // the stop that goes with it (PLAN.md §3).
      stop_with(stop_reason::unimplemented_service,
                cpu::physical_address(service::stub_segment, at));
      return;
    }

    if (cpu_.regs()[cpu::sreg::cs] == service::stub_segment &&
        cpu_.regs().ip == at) {
      return;
    }
  }
}

void machine::note_service_call(const service_call& call) noexcept {
  last_service_call_ = call;
  have_service_call_ = true;
  trace_.record(call);
}

bool machine::stop_unimplemented_service(std::uint32_t at) {
  return stop_with(stop_reason::unimplemented_service, at);
}

void machine::exit_program(std::uint8_t code) {
  stop_ = {.reason = stop_reason::program_exited, .exit_code = code};
  if (log_ != nullptr) {
    log_->report(stop_);
  }
}

bool machine::stop_with(stop_reason reason, std::uint32_t at) {
  stop_ = {.reason = reason, .at = at};
  if (log_ != nullptr) {
    log_->report(stop_);
  }
  return false;
}

void machine::notice_memory(notice_kind what, std::uint32_t address,
                            std::uint8_t value) {
  // Marked before the sink is consulted, and marked whether or not there
  // is one: what the machine does must not depend on who is watching.
  // An address above the megabyte has no page to mark — classify() calls
  // it open bus rather than pretending it is somewhere — so it is
  // reported every time, which is fine for a thing that cannot happen
  // without a bug in the caller.
  const bool first = address >= cpu::address_space_size ||
                     first_touch(pages_noticed_, address / notice_page_size);
  if (!first || log_ == nullptr) {
    return;
  }

  log_->report({.what = what,
                .at = address,
                .value = value,
                .cs = cpu_.regs()[cpu::sreg::cs],
                .ip = cpu_.current().start_ip});
}

void machine::notice_port(notice_kind what, std::uint16_t port,
                          std::uint8_t value) {
  if (!first_touch(ports_noticed_, port) || log_ == nullptr) {
    return;
  }

  log_->report({.what = what,
                .at = port,
                .value = value,
                .cs = cpu_.regs()[cpu::sreg::cs],
                .ip = cpu_.current().start_ip});
}

void machine::notice_video_mode(std::uint8_t mode) {
  if (!first_touch(video_modes_noticed_, mode) || log_ == nullptr) {
    return;
  }

  log_->report({.what = notice_kind::undisplayable_video_mode,
                .at = mode,
                .value = mode,
                .cs = cpu_.regs()[cpu::sreg::cs],
                .ip = cpu_.current().start_ip});
}

void machine::note_device_fault(device& dev) {
  // Already stopped is the common case for every cycle a caller takes
  // past the first fault (device.h, #65) — the same guard `step()` gives
  // a processor stop, so a fault does not clobber whatever stop already
  // explains why the machine is not running.
  if (stopped() || !dev.faulted()) {
    return;
  }

  const device_fault& fault = dev.fault();
  // Built and kept whether or not a sink is listening, for the reason
  // `service_floor::call()` builds its record before the branch: what the
  // machine remembers about why it stopped must not depend on who was
  // watching when it did.
  last_device_stop_ = device_stop{.at = fault.at,
                                  .detail = fault.detail,
                                  .cs = cpu_.regs()[cpu::sreg::cs],
                                  .ip = cpu_.current().start_ip};
  have_device_stop_ = true;
  if (log_ != nullptr) {
    // The detailed record first, same order dispatch_services() reports
    // an unimplemented service before stop_with()'s generic line: this
    // one names the port or address and the byte the device chose to say
    // about it, which the generic stop_record cannot.
    log_->report(last_device_stop_);
  }
  stop_with(stop_reason::unimplemented_device, fault.at);
}

}  // namespace amberfolio::machine
