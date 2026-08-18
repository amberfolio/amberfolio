// SPDX-License-Identifier: AGPL-3.0-only
//
// The device and the diagnostics sink the machine tests wire up: one that
// claims whatever it is told to and remembers every cycle it was given,
// and one that keeps everything it was told rather than printing it.
//
// There is no real device in the tree yet — every one of them is an M2-D
// issue — so what these tests can check about routing is that a cycle
// arrives at the device that claimed the address, with the address the
// bus was given. That is the whole of M2-F1's promise; what a device then
// does with it is the device's own issue and its own tests.
//
// Test-side code, so the rule that keeps core/ free of host dependencies
// does not apply: std::vector is fine.

#pragma once

#include <cstdint>
#include <vector>

#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/diagnostics.h"

namespace amberfolio::machine::test {

/// One cycle the machine handed to a device.
struct device_access {
  enum class kind : std::uint8_t {
    read_memory,
    write_memory,
    read_port,
    write_port,
  };

  kind what{};
  std::uint32_t at{};
  std::uint8_t value{};

  friend bool operator==(const device_access&, const device_access&) = default;
};

class recording_device final : public device {
 public:
  /// Ask for a window or a range. Before `machine::attach()`, which is
  /// when these are read.
  void wants(memory_window window) { windows_.push_back(window); }
  void wants(port_range range) { ports_.push_back(range); }

  [[nodiscard]] claims claimed() const noexcept override {
    return {.memory = windows_, .ports = ports_};
  }

  void reset() override { ++resets; }

  std::uint8_t read_memory(std::uint32_t address) override {
    accesses.push_back({.what = device_access::kind::read_memory,
                        .at = address,
                        .value = answer});
    return answer;
  }

  void write_memory(std::uint32_t address, std::uint8_t value) override {
    accesses.push_back({.what = device_access::kind::write_memory,
                        .at = address,
                        .value = value});
  }

  std::uint8_t read_port(std::uint16_t port) override {
    accesses.push_back(
        {.what = device_access::kind::read_port, .at = port, .value = answer});
    return answer;
  }

  void write_port(std::uint16_t port, std::uint8_t value) override {
    accesses.push_back(
        {.what = device_access::kind::write_port, .at = port, .value = value});
  }

  /// What every read of this device answers. Not FF, so that a test can
  /// tell the device's answer from open bus.
  std::uint8_t answer{0x5A};

  std::vector<device_access> accesses;
  unsigned resets{};

 private:
  std::vector<memory_window> windows_;
  std::vector<port_range> ports_;
};

/// The machine's one sink, keeping all three kinds of thing it is told.
class recording_diagnostics final : public diagnostics {
 public:
  void report(const notice& what) override { notices.push_back(what); }
  void report(const stop_record& stop) override { stops.push_back(stop); }
  void report(const cpu::stop_record& stop) override {
    processor_stops.push_back(stop);
  }

  std::vector<notice> notices;
  std::vector<stop_record> stops;
  std::vector<cpu::stop_record> processor_stops;
};

}  // namespace amberfolio::machine::test
