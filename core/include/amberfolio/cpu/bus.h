// SPDX-License-Identifier: AGPL-3.0-only
//
// The bus the CPU executes against: memory and I/O ports, and nothing
// else. The conformance harness implements it over a flat array of test
// vectors (M1-F4); the machine implements it over real RAM and the device
// map (M2). The CPU never knows which.
//
// Memory is byte-wide on purpose. The 8088 has an 8-bit data bus, every
// word access is two byte cycles, and the SingleStepTests vectors are 8088
// silicon — modelling a 16-bit memory port here would make the CPU's own
// access pattern unobservable and put us one abstraction away from the
// oracle. The port side does offer a 16-bit call, because a 16-bit device
// on the port bus (which the PC does have) answers a word IN in one go and
// a bus that wants to say so must be able to.
//
// Addresses arriving here are already physical and already wrapped to 20
// bits: forming (segment << 4) + offset is the CPU's job, not the bus's
// (see address.h). An implementation may index a 1 MiB array directly.

#pragma once

#include <cstdint>

namespace amberfolio::cpu {

/// Memory and I/O, as the processor sees them.
class bus {
 public:
  bus() = default;
  bus(const bus&) = delete;
  bus(bus&&) = delete;
  bus& operator=(const bus&) = delete;
  bus& operator=(bus&&) = delete;

  /// Read one byte of memory. `address` is physical and < 0x100000.
  [[nodiscard]] virtual std::uint8_t read_memory(std::uint32_t address) = 0;

  /// Write one byte of memory. `address` is physical and < 0x100000.
  virtual void write_memory(std::uint32_t address, std::uint8_t value) = 0;

  [[nodiscard]] virtual std::uint8_t read_port8(std::uint16_t port) = 0;
  virtual void write_port8(std::uint16_t port, std::uint8_t value) = 0;

  /// Word I/O. The default is what an 8-bit peripheral gives you: two byte
  /// cycles, low half at `port` and high half at `port + 1`, wrapping in
  /// 16 bits. A bus whose device answers a word in one transfer overrides
  /// these; one that does not, does not have to think about it.
  [[nodiscard]] virtual std::uint16_t read_port16(std::uint16_t port) {
    const auto low = static_cast<unsigned>(read_port8(port));
    const auto high =
        static_cast<unsigned>(read_port8(static_cast<std::uint16_t>(port + 1)));
    return static_cast<std::uint16_t>(low | (high << 8u));
  }

  virtual void write_port16(std::uint16_t port, std::uint16_t value) {
    write_port8(port, static_cast<std::uint8_t>(value));
    write_port8(static_cast<std::uint16_t>(port + 1),
                static_cast<std::uint8_t>(value >> 8u));
  }

 protected:
  // Non-virtual and protected: a bus is held by reference and never owned
  // or deleted through this type, so it pays for no vtable slot it does
  // not need. (This is the pattern -Wnon-virtual-dtor exists to steer you
  // towards, not the one it warns about.)
  ~bus() = default;
};

}  // namespace amberfolio::cpu
