// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/machine.h"

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
    : memory_(layout), log_(log), cpu_(*this) {}

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

bool machine::attach(scheduled& who) {
  if (!deadlines_.add(who)) {
    return stop_with(stop_reason::conflicting_claim, 0);
  }
  return true;
}

void machine::reset() {
  // The clock first, so that a device arming a deadline from its own
  // reset() arms it against the time base the run is about to start on
  // rather than against the one that just ended.
  now_ = 0;
  deadlines_.disarm_all();

  cpu_.reset();
  for (std::size_t i = 0; i < attached_; ++i) {
    devices_[i]->reset();
  }

  stop_ = {};
  pages_noticed_ = {};
  ports_noticed_ = {};
}

bool machine::set_step_cost(ticks cost) noexcept {
  if (cost == 0) {
    return false;
  }
  step_cost_ = cost;
  return true;
}

cpu::step_status machine::step() {
  if (stopped()) {
    return cpu::step_status::stopped;
  }

  // Before the instruction, not after: this is the step boundary, and the
  // interrupt a device raises here is recognized by the very step below.
  deadlines_.dispatch_due(now_);

  const cpu::step_status status = cpu_.step();
  if (status != cpu::step_status::stopped) {
    // Charged for every status the processor can report, `halted`
    // included: a halted machine is waiting for an interrupt, and the
    // only thing that can bring one is a deadline, which needs the clock
    // to keep moving. A stop is the one thing that costs nothing —
    // nothing happened, and a caller looping past it must not be able to
    // run the clock away.
    now_ += step_cost_;
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

  result.elapsed = now_ - started;
  return result;
}

std::uint8_t machine::read_memory(std::uint32_t address) {
  switch (memory_.classify(address)) {
    case region::ram:
    case region::rom:
      return memory_.ram()[address];
    case region::device:
      return memory_.owner(address)->read_memory(address);
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
    case region::device:
      memory_.owner(address)->write_memory(address, value);
      return;
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
    return dev->read_port(port);
  }

  notice_port(notice_kind::unclaimed_port_read, port, 0);
  return open_bus_value;
}

void machine::write_port8(std::uint16_t port, std::uint8_t value) {
  if (device* dev = ports_.owner(port); dev != nullptr) {
    dev->write_port(port, value);
    return;
  }

  notice_port(notice_kind::unclaimed_port_write, port, value);
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

}  // namespace amberfolio::machine
