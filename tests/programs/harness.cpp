// SPDX-License-Identifier: AGPL-3.0-only

#include "programs/harness.h"

#include <chrono>
#include <cstddef>
#include <memory>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/machine/machine.h"

namespace amberfolio::programs {
namespace {

/// Power-on, then the one departure from it: the segment registers and SP
/// the layout says a program starts with.
void start(cpu::processor& cpu) {
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
}

/// Step until the program halts, the interpreter refuses something, or the
/// cap runs out — and time only that. Both runners share it, so the step
/// model and the clock cannot differ between them.
template <typename Step>
void drive(outcome& result, std::uint64_t step_cap, Step step) {
  const auto started = std::chrono::steady_clock::now();
  while (result.steps < step_cap) {
    const cpu::step_status status = step();
    if (status == cpu::step_status::halted ||
        status == cpu::step_status::stopped) {
      break;
    }
    ++result.steps;
  }
  const auto ended = std::chrono::steady_clock::now();

  result.seconds = std::chrono::duration<double>(ended - started).count();
  result.capped = result.steps >= step_cap;
}

/// The machine's single sink, filtered down to what an outcome carries:
/// the processor's stop, and a count of everything the machine was asked
/// for that nothing answers for.
class machine_log final : public machine::diagnostics {
 public:
  void report(const machine::notice& /*what*/) override { ++notices; }
  void report(const machine::service_call& /*call*/) override {}
  void report(const machine::stop_record& /*stop*/) override {}
  void report(const cpu::stop_record& stop) override { record = stop; }
  void report(const machine::device_stop& /*stop*/) override {}

  cpu::stop_record record{};
  std::uint64_t notices{};
};

}  // namespace

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

  start(cpu);

  outcome result;
  drive(result, step_cap, [&cpu] { return cpu.step(); });

  result.regs = cpu.regs();
  result.stop = log.record;
  result.halted = cpu.halted();
  return result;
}

outcome run_on_machine(std::span<const std::uint8_t> code,
                       std::uint64_t step_cap) {
  machine_log log;

  // On the heap: a machine has a megabyte of RAM inside it, and a
  // megabyte of automatic storage is over the default stack on one of the
  // four targets.
  auto box =
      std::make_unique<machine::machine>(machine::memory_layout::flat, &log);
  machine::machine& pc = *box;

  // Through memory().ram(), not through the bus: this is the machine
  // loading a program, not the program writing memory, and the two are
  // different acts (memory_map.h). The address arithmetic is the same one
  // the program will be fetched through, as above.
  for (std::size_t i = 0; i < code.size(); ++i) {
    pc.memory().ram()[cpu::physical_address(
        layout::code_segment, static_cast<std::uint16_t>(i))] = code[i];
  }

  pc.reset();
  start(pc.processor());

  outcome result;
  drive(result, step_cap, [&pc] { return pc.step(); });

  result.regs = pc.processor().regs();
  result.stop = log.record;
  result.notices = log.notices;
  result.halted = pc.processor().halted();
  return result;
}

}  // namespace amberfolio::programs
