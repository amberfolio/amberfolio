// SPDX-License-Identifier: AGPL-3.0-only
//
// The bus and the diagnostics sink the CPU tests execute against: a flat
// megabyte of RAM, a port map, and a record of everything that was asked
// for. Shared by every test in this directory, and by the conformance
// harness's own unit tests later — a fixture with a memory of what
// happened is what lets a test assert that the CPU touched the bus the way
// the part would have, not merely that it produced the right answer.
//
// Test-side code, so the freestanding rule that binds core/ does not apply
// here: std::vector and std::map are fine.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/diagnostics.h"

namespace amberfolio::cpu::test {

/// One thing the CPU asked memory to do.
struct memory_access {
  enum class kind : std::uint8_t { read, write };

  kind what{};
  std::uint32_t address{};
  std::uint8_t value{};

  friend bool operator==(const memory_access&, const memory_access&) = default;
};

/// One thing the CPU asked the port bus to do.
struct port_access {
  enum class kind : std::uint8_t { read8, write8, read16, write16 };

  kind what{};
  std::uint16_t port{};
  std::uint16_t value{};

  friend bool operator==(const port_access&, const port_access&) = default;
};

class test_bus final : public bus {
 public:
  std::uint8_t read_memory(std::uint32_t address) override {
    // .at(), not []: the CPU promises physical addresses below 1 MiB, and
    // a test is the right place for that promise to fail loudly.
    const std::uint8_t value = memory_.at(address);
    accesses.push_back({.what = memory_access::kind::read,
                        .address = address,
                        .value = value});
    return value;
  }

  void write_memory(std::uint32_t address, std::uint8_t value) override {
    memory_.at(address) = value;
    accesses.push_back({.what = memory_access::kind::write,
                        .address = address,
                        .value = value});
  }

  std::uint8_t read_port8(std::uint16_t port) override {
    const std::uint8_t value = port_value(port);
    ports.push_back(
        {.what = port_access::kind::read8, .port = port, .value = value});
    return value;
  }

  void write_port8(std::uint16_t port, std::uint8_t value) override {
    port_state[port] = value;
    ports.push_back(
        {.what = port_access::kind::write8, .port = port, .value = value});
  }

  /// Put bytes at a physical address. Test setup, not machine activity:
  /// poke and peek write and read `memory_` directly, so they leave no
  /// trace in `accesses`.
  void poke(std::uint32_t address, std::initializer_list<std::uint8_t> bytes) {
    std::uint32_t at = address;
    for (const std::uint8_t byte : bytes) {
      memory_.at(at & address_mask) = byte;
      ++at;
    }
  }

  /// Put bytes at segment:offset, wrapping the offset in 16 bits the way
  /// the machine does.
  void poke(std::uint16_t segment, std::uint16_t offset,
            std::initializer_list<std::uint8_t> bytes) {
    std::uint16_t at = offset;
    for (const std::uint8_t byte : bytes) {
      memory_.at(physical_address(segment, at)) = byte;
      at = static_cast<std::uint16_t>(at + 1);
    }
  }

  [[nodiscard]] std::uint8_t peek(std::uint32_t address) const {
    return memory_.at(address);
  }

  [[nodiscard]] std::uint8_t peek(std::uint16_t segment,
                                  std::uint16_t offset) const {
    return memory_.at(physical_address(segment, offset));
  }

  /// What a read of `port` will answer. Unset ports read as 0xFF, which is
  /// what an unclaimed ISA port floats to.
  std::map<std::uint16_t, std::uint8_t> port_state;

  /// Every memory access the CPU made, in order. A word access is two
  /// entries — the bus is eight bits wide and that has to stay visible.
  std::vector<memory_access> accesses;

  /// Every port access, in order.
  std::vector<port_access> ports;

 private:
  [[nodiscard]] std::uint8_t port_value(std::uint16_t port) const {
    const auto found = port_state.find(port);
    return found == port_state.end() ? std::uint8_t{0xFF} : found->second;
  }

  std::vector<std::uint8_t> memory_ =
      std::vector<std::uint8_t>(address_space_size, 0);
};

/// A diagnostics sink that keeps what it was told.
class recording_diagnostics final : public diagnostics {
 public:
  void unimplemented_opcode(const stop_record& stop) override {
    reports.push_back(stop);
  }

  std::vector<stop_record> reports;
};

}  // namespace amberfolio::cpu::test
