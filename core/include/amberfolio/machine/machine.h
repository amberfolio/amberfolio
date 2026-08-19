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
// What is deliberately not here yet:
//
//   * **Time.** `step()` is a passthrough. Virtual time, the step cost, a
//     device deadline queue and the paced `run()` loop are M2-F2 (#43);
//     the machine is where they will live, and nothing here presumes
//     their shape.
//   * **The BIOS.** The map reserves and backs F0000-FFFFF; what goes in
//     it — the vector table, the callout stubs, the BDA — is M2-F3 (#44).
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
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/port_map.h"

namespace amberfolio::machine {

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

  /// The RESET line: the processor and every attached device go to
  /// power-on state, the recorded stop is cleared, and the maps start
  /// noticing untouched pages and ports again.
  ///
  /// Memory keeps what it held. That is what the line does — RESET on a
  /// PC does not clear RAM, which is how a warm boot can be told from a
  /// cold one — and a machine that must start from nothing is
  /// constructed, not reset. Attached devices stay attached: what they
  /// claimed is how the machine is wired, not part of its state.
  void reset();

  /// One scheduling step of the processor: one instruction, one iteration
  /// of a repeated string instruction, or one interrupt delivery
  /// (cpu::step_status).
  ///
  /// Nothing else happens here. What a step costs in virtual time and
  /// which device deadlines fall due while it runs is the scheduler's
  /// business (M2-F2, #43), and this function deliberately does not know.
  ///
  /// A stopped machine keeps answering `stopped` and touches neither the
  /// bus nor the devices, so a caller that does not check can loop
  /// harmlessly rather than execute past the thing it was told about.
  cpu::step_status step();

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

  /// True once the machine has stopped, for its own reason or because the
  /// processor did. Sticky until `reset()`.
  [[nodiscard]] bool stopped() const noexcept {
    return stop_.reason != stop_reason::none;
  }

  [[nodiscard]] const stop_record& stop() const noexcept { return stop_; }

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
  /// Record a stop, tell the sink once, and answer false so that
  /// `attach()` can `return` it.
  bool stop_with(stop_reason reason, std::uint32_t at);

  /// Report `what`, if this is the first time anything has been asked of
  /// that page or that port since the last reset. Fills in where the
  /// program was; the caller supplies the rest.
  void notice_memory(notice_kind what, std::uint32_t address,
                     std::uint8_t value);
  void notice_port(notice_kind what, std::uint16_t port, std::uint8_t value);

  memory_map memory_;
  port_map ports_;
  diagnostics* log_;

  std::array<device*, max_devices> devices_{};
  std::size_t attached_{};

  cpu::processor cpu_;
  stop_record stop_{};

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
};

}  // namespace amberfolio::machine
