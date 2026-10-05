// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine this host runs and the sink it reports to (#472, split from
// main.cpp): every device in the order it is constructed, and the
// diagnostics that put what the core would not fake on stderr.

#pragma once

#include <array>
#include <cstdio>
#include <memory>

#include "amberfolio/host/slot_store.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/speaker.h"
#include "amberfolio/machine/tandy_sound.h"

namespace amberfolio::sdl {

/// Everything the machine is made of, in one place so its construction
/// order is visible: the PIC exists before the PIT that raises IRQ0
/// through it, and the PIT before the speaker that gates channel 2.
struct wired_machine {
  explicit wired_machine(machine::diagnostics* log);

  std::unique_ptr<machine::machine> box;
  machine::pic::controller irq;
  machine::pit timer;
  machine::speaker spk;
  std::unique_ptr<machine::ega> video;
  machine::renderer render;
  machine::tandy_sound chip;
};

/// Reports what the core would not fake, to stderr. A host has to have
/// one of these or "log, don't fake" is only half a mechanism
/// (machine/diagnostics.h).
///
/// **The sentences are not this host's.** Every line below is rendered by
/// `machine::format_diagnostic` (machine/report.h), for the reason that
/// file gives at length: the browser has a sink of its own now
/// (machine/log.h, M4-W1 #108), and two hosts writing their own version
/// of the same line would produce two accounts that look alike and can
/// quietly differ. This host's job is to put the characters on stderr.
class stderr_diagnostics final : public machine::diagnostics {
 public:
  /// The playthrough's sidecars, if this run has them (M5-E2c #173,
  /// #351).
  ///
  /// It is fed from here because this is where the DOS layer's file
  /// events arrive, and the store learns which save slot the program
  /// touched from nothing else — the program keeps no slot letter in
  /// memory to read (`hosts/common/.../slot_store.h`). Null on every
  /// run that did not ask for it, which is every run in `tests/sessions`.
  void set_slot_store(host::slot_store* store) noexcept { slots_ = store; }

  /// Whether to print every service call and file event as it happens.
  /// Off by default, for the reason diagnostics.h gives: a call is
  /// something the program *did*, not a symptom of anything, and a boot
  /// makes tens of thousands of them. `--trace` turns it on, so that the
  /// live stream and the ring dumped at the end are one facility asked
  /// for once.
  void set_tracing(bool on) noexcept { tracing_ = on; }

  void report(const machine::notice& what) override;
  void report(const machine::stop_record& stop) override;
  void report(const cpu::stop_record& stop) override;
  void report(const machine::device_stop& stop) override;
  void report(const machine::seam_event& event) override;
  void report(const machine::file_event& event) override;
  void report(const machine::service_call& call) override;

 private:
  /// Render one record and put it on stderr. Nothing is printed for a
  /// record that has no line.
  template <typename T>
  void write(const T& record) noexcept {
    std::array<char, machine::diagnostic_line_capacity> line{};
    if (machine::format_diagnostic(record, line) == 0) {
      return;
    }
    std::fputs(line.data(), stderr);
  }

  host::slot_store* slots_{nullptr};

  bool tracing_{false};
};

}  // namespace amberfolio::sdl
