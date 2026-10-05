// SPDX-License-Identifier: AGPL-3.0-only

#include "wiring.h"

#include <array>
#include <cstdio>
#include <memory>

#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/report.h"

namespace amberfolio::sdl {

wired_machine::wired_machine(machine::diagnostics* log)
    : box(std::make_unique<machine::machine>(machine::memory_layout::pc, log)),
      irq(*box),
      timer(*box, irq),
      spk(*box, timer),
      video(std::make_unique<machine::ega>(*box)),
      render(*box, *video),
      chip(*box) {
  box->attach(irq);
  box->attach(timer);
  box->attach(spk);
  box->attach(*video);
  box->attach(chip);

  box->schedule(timer.channel0_deadline());
  box->schedule(timer.channel2_deadline());
  box->schedule(spk);
  box->schedule(render);

  machine::install_int10(box->services());
  machine::install_dos_services(box->services());

  box->reset();
  render.reset();
}

void stderr_diagnostics::report(const machine::notice& what) { write(what); }

void stderr_diagnostics::report(const machine::stop_record& stop) {
  // A program exiting produces no line at all — report.h owns that
  // rule, so that this host and the browser agree about it.
  write(stop);
}

void stderr_diagnostics::report(const cpu::stop_record& stop) { write(stop); }

void stderr_diagnostics::report(const machine::device_stop& stop) {
  write(stop);
}

void stderr_diagnostics::report(const machine::seam_event& event) {
  write(event);
}

void stderr_diagnostics::report(const machine::file_event& event) {
  if (slots_ != nullptr) {
    slots_->saw(event);
  }
  if (!tracing_) {
    return;
  }
  write(event);
}

void stderr_diagnostics::report(const machine::service_call& call) {
  if (!tracing_) {
    return;
  }
  write(call);
}

}  // namespace amberfolio::sdl
