// SPDX-License-Identifier: AGPL-3.0-only

#include "memory_dump.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <span>
#include <string>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/machine/machine.h"

namespace amberfolio::sdl {

std::string registers_line(const machine::machine& box) {
  const auto& r = box.processor().regs();
  std::array<char, 160> text{};
  std::snprintf(text.data(), text.size(),
                "cs=%04X ip=%04X ds=%04X es=%04X ss=%04X sp=%04X bp=%04X"
                " ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X\n",
                r[cpu::sreg::cs], r.ip, r[cpu::sreg::ds], r[cpu::sreg::es],
                r[cpu::sreg::ss], r[cpu::reg16::sp], r[cpu::reg16::bp],
                r[cpu::reg16::ax], r[cpu::reg16::bx], r[cpu::reg16::cx],
                r[cpu::reg16::dx], r[cpu::reg16::si], r[cpu::reg16::di]);
  return text.data();
}

bool write_memory(const std::string& stem, const machine::machine& box) {
  const std::span<const std::uint8_t> ram = box.memory().ram();
  std::ofstream memory(stem + ".ram", std::ios::binary | std::ios::trunc);
  memory.write(reinterpret_cast<const char*>(ram.data()),
               static_cast<std::streamsize>(ram.size()));
  std::ofstream registers(stem + ".regs", std::ios::trunc);
  registers << registers_line(box);
  return static_cast<bool>(memory) && static_cast<bool>(registers);
}

}  // namespace amberfolio::sdl
