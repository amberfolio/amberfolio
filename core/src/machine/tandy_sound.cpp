// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/tandy_sound.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/psg.h"
#include "amberfolio/machine/state.h"

namespace amberfolio::machine {

void tandy_sound::reset() { regs_ = psg_registers::powered_on(); }

void tandy_sound::save_state(state_sink& out) const {
  out.u8(regs_.latched);
  for (std::size_t voice = 0; voice < psg_voices; ++voice) {
    out.u16(regs_.period[voice]);
    out.u8(regs_.attenuation[voice]);
  }
  out.u8(regs_.noise);
}

void tandy_sound::write_port(std::uint16_t /*port*/, std::uint8_t value) {
  static_cast<void>(regs_.write(value));
  box_->audio().publish_chip(box_->time(), value);
}

}  // namespace amberfolio::machine
