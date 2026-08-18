// SPDX-License-Identifier: AGPL-3.0-only

#include "programs/harness.h"

#include <chrono>
#include <cstddef>

#include "amberfolio/cpu/processor.h"

namespace amberfolio::programs {

flat_bus::flat_bus() : memory_(cpu::address_space_size, 0) {}

std::uint8_t flat_bus::read_memory(std::uint32_t address) {
  return memory_[address];
}

void flat_bus::write_memory(std::uint32_t address, std::uint8_t value) {
  memory_[address] = value;
}

std::uint8_t flat_bus::read_port8(std::uint16_t /*port*/) { return 0xFF; }

void flat_bus::write_port8(std::uint16_t /*port*/, std::uint8_t /*value*/) {}

outcome run(std::span<const std::uint8_t> code, std::uint64_t step_cap) {
  flat_bus memory;
  recorded_stop log;
  cpu::processor cpu(memory, &log);

  // Placed through the segment:offset arithmetic the program will itself
  // be fetched through, so a program is loaded where it runs by
  // construction rather than by two calculations agreeing. Every program
  // here is a few dozen bytes; one that outgrew a segment would need a
  // loader, which is M2's job and not this file's.
  for (std::size_t i = 0; i < code.size(); ++i) {
    memory.write_memory(cpu::physical_address(layout::code_segment,
                                              static_cast<std::uint16_t>(i)),
                        code[i]);
  }

  // reset() is what the constructor already did; called again so that this
  // reads as the power-on state it is, and so that the register loads
  // below are visibly the only departure from it.
  cpu.reset();
  cpu.regs()[cpu::sreg::cs] = layout::code_segment;
  cpu.regs()[cpu::sreg::ds] = layout::data_segment;
  cpu.regs()[cpu::sreg::es] = layout::data_segment;
  cpu.regs()[cpu::sreg::ss] = layout::stack_segment;
  cpu.regs()[cpu::reg16::sp] = layout::stack_pointer;
  cpu.regs().ip = 0;

  outcome result;
  const auto started = std::chrono::steady_clock::now();
  while (result.steps < step_cap) {
    const cpu::step_status status = cpu.step();
    if (status == cpu::step_status::halted ||
        status == cpu::step_status::stopped) {
      break;
    }
    ++result.steps;
  }
  const auto ended = std::chrono::steady_clock::now();

  result.seconds = std::chrono::duration<double>(ended - started).count();
  result.regs = cpu.regs();
  result.stop = log.record;
  result.halted = cpu.halted();
  result.capped = result.steps >= step_cap;
  return result;
}

}  // namespace amberfolio::programs
