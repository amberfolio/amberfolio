// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/cpu/processor.h"

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

processor::processor(bus& machine_bus, diagnostics* log) noexcept
    : bus_(&machine_bus), log_(log) {
  reset();
}

void processor::reset() noexcept {
  regs_ = registers{};
  regs_[sreg::cs] = 0xFFFF;
  regs_.flags = flag::reset_value;
  stop_ = stop_record{};
  halted_ = false;
}

std::uint8_t processor::read_byte(std::uint16_t segment, std::uint16_t offset) {
  return bus_->read_memory(physical_address(segment, offset));
}

void processor::write_byte(std::uint16_t segment, std::uint16_t offset,
                           std::uint8_t value) {
  bus_->write_memory(physical_address(segment, offset), value);
}

std::uint8_t processor::fetch_byte() {
  const std::uint8_t byte = read_byte(regs_[sreg::cs], regs_.ip);
  regs_.ip = static_cast<std::uint16_t>(regs_.ip + 1);
  return byte;
}

step_status processor::stop_unimplemented(std::uint8_t opcode,
                                          std::uint16_t start_ip) {
  regs_.ip = start_ip;
  stop_ = stop_record{.reason = stop_reason::unimplemented_opcode,
                      .opcode = opcode,
                      .cs = regs_[sreg::cs],
                      .ip = start_ip};
  if (log_ != nullptr) {
    log_->unimplemented_opcode(stop_);
  }
  return step_status::stopped;
}

step_status processor::step() {
  // Both checked before anything is fetched, so neither state can be
  // walked out of by accident: a stopped processor touches no bus, and a
  // halted one consumes no instruction.
  if (stopped()) {
    return step_status::stopped;
  }
  if (halted_) {
    return step_status::halted;
  }

  const std::uint16_t start_ip = regs_.ip;
  const std::uint8_t opcode = fetch_byte();

  // M1-F3 puts the decoder and the dispatch table here. Until it does,
  // every opcode is one with no handler, and that is exactly what the
  // machine should say about it — the unimplemented path is not a
  // placeholder, it is the permanent floor under a partly-filled table.
  return stop_unimplemented(opcode, start_ip);
}

}  // namespace amberfolio::cpu
