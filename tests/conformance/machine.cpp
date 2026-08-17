// SPDX-License-Identifier: AGPL-3.0-only

#include "machine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "vectors.h"

namespace amberfolio::conformance {
namespace {

/// The most steps one vector can possibly need. A repeated string
/// instruction is one step per iteration (PLAN.md §3) and the suite masks
/// CX to seven bits for those, so 127 iterations plus the one that
/// retires is the real ceiling; the rest is headroom before a runaway
/// loop is called what it is.
constexpr int step_ceiling = 200;

[[nodiscard]] std::string hex(std::uint32_t value, int digits) {
  static constexpr std::string_view nibbles = "0123456789ABCDEF";
  std::string out(static_cast<std::size_t>(digits), '0');
  for (int i = digits - 1; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = nibbles[value & 0xFu];
    value >>= 4u;
  }
  return out;
}

[[nodiscard]] std::string reason_text(cpu::stop_reason reason) {
  switch (reason) {
    case cpu::stop_reason::unimplemented_opcode:
      return "no handler for it in the dispatch table";
    case cpu::stop_reason::prefix_chain_too_long:
      return "the prefix run never reached an opcode";
    case cpu::stop_reason::none:
      break;
  }
  return "an unnamed reason, which is itself a bug";
}

/// The registers a diff walks, in the order a human reads them — which
/// is not the order the encoding numbers them in, and not the order the
/// vectors list them in either.
struct named_register {
  enum class which : std::uint8_t { word, segment, ip };

  std::string_view name;
  which kind;
  std::uint8_t index;  ///< a reg16 or an sreg; unused for IP
};

[[nodiscard]] std::uint16_t value_of(const cpu::registers& regs,
                                     const named_register& entry) {
  switch (entry.kind) {
    case named_register::which::word:
      return regs[static_cast<cpu::reg16>(entry.index)];
    case named_register::which::segment:
      return regs[static_cast<cpu::sreg>(entry.index)];
    case named_register::which::ip:
      break;
  }
  return regs.ip;
}

constexpr named_register word_register(std::string_view name, cpu::reg16 reg) {
  return {.name = name,
          .kind = named_register::which::word,
          .index = static_cast<std::uint8_t>(reg)};
}

constexpr named_register segment_register(std::string_view name,
                                          cpu::sreg reg) {
  return {.name = name,
          .kind = named_register::which::segment,
          .index = static_cast<std::uint8_t>(reg)};
}

constexpr std::array<named_register, 13> register_order = {{
    word_register("AX", cpu::reg16::ax),
    word_register("BX", cpu::reg16::bx),
    word_register("CX", cpu::reg16::cx),
    word_register("DX", cpu::reg16::dx),
    word_register("SP", cpu::reg16::sp),
    word_register("BP", cpu::reg16::bp),
    word_register("SI", cpu::reg16::si),
    word_register("DI", cpu::reg16::di),
    segment_register("CS", cpu::sreg::cs),
    segment_register("DS", cpu::sreg::ds),
    segment_register("ES", cpu::sreg::es),
    segment_register("SS", cpu::sreg::ss),
    {.name = "IP", .kind = named_register::which::ip, .index = 0},
}};

/// Which flag bits differ, spelled out. "the flags disagree" is not a
/// diagnosis; "AF" is.
[[nodiscard]] std::string flag_difference(std::uint16_t want,
                                          std::uint16_t got) {
  static constexpr std::array<std::pair<std::uint16_t, std::string_view>, 9>
      bits = {{{cpu::flag::of, "OF"},
               {cpu::flag::df, "DF"},
               {cpu::flag::if_, "IF"},
               {cpu::flag::tf, "TF"},
               {cpu::flag::sf, "SF"},
               {cpu::flag::zf, "ZF"},
               {cpu::flag::af, "AF"},
               {cpu::flag::pf, "PF"},
               {cpu::flag::cf, "CF"}}};

  const auto differ = static_cast<std::uint16_t>(want ^ got);
  std::string out;
  for (const auto& [bit, name] : bits) {
    if ((differ & bit) != 0) {
      if (!out.empty()) {
        out += ' ';
      }
      out += name;
    }
  }
  if ((differ & ~cpu::flag::defined) != 0) {
    if (!out.empty()) {
      out += ' ';
    }
    // The hardwired bits cannot legitimately move; if they have, the
    // fault is in whatever wrote the flag word, not in an instruction.
    out += "a hardwired bit";
  }
  return out;
}

void diff_registers(const cpu::registers& want, const cpu::registers& got,
                    std::string& report) {
  for (const named_register& entry : register_order) {
    const std::uint16_t expected = value_of(want, entry);
    const std::uint16_t actual = value_of(got, entry);
    if (expected != actual) {
      report += "  ";
      report += entry.name;
      report +=
          "    expected " + hex(expected, 4) + "  got " + hex(actual, 4) + "\n";
    }
  }
  if (want.flags != got.flags) {
    report += "  FLAGS expected " + hex(want.flags, 4) + " [" +
              flag_letters(want.flags) + "]  got " + hex(got.flags, 4) + " [" +
              flag_letters(got.flags) +
              "]  differ: " + flag_difference(want.flags, got.flags) + "\n";
  }
}

}  // namespace

std::string flag_letters(std::uint16_t flags) {
  static constexpr std::array<std::pair<std::uint16_t, char>, 9> bits = {
      {{cpu::flag::of, 'O'},
       {cpu::flag::df, 'D'},
       {cpu::flag::if_, 'I'},
       {cpu::flag::tf, 'T'},
       {cpu::flag::sf, 'S'},
       {cpu::flag::zf, 'Z'},
       {cpu::flag::af, 'A'},
       {cpu::flag::pf, 'P'},
       {cpu::flag::cf, 'C'}}};

  std::string out;
  out.reserve(bits.size());
  for (const auto& [bit, letter] : bits) {
    out += (flags & bit) != 0 ? letter : '.';
  }
  return out;
}

// --- The bus ----------------------------------------------------------

vector_bus::vector_bus()
    : value_(cpu::address_space_size, 0),
      expected_(cpu::address_space_size, 0),
      state_(cpu::address_space_size, 0) {}

void vector_bus::mark(std::uint32_t address, std::uint8_t flag) {
  std::uint8_t& state = state_[address];
  if (state == 0) {
    touched_.push_back(address);
  }
  state = static_cast<std::uint8_t>(state | flag);
}

void vector_bus::begin(const vector_test& test) {
  for (const std::uint32_t address : touched_) {
    state_[address] = 0;
  }
  touched_.clear();
  faults_.clear();
  port_index_ = 0;
  test_ = &test;

  for (const memory_byte& byte : test.ram_before) {
    const std::uint32_t address = byte.address & cpu::address_mask;
    value_[address] = byte.value;
    expected_[address] = byte.value;
    mark(address, mapped | expected);
  }
  // Second, so that a byte listed in both takes its final value here.
  for (const memory_byte& byte : test.ram_after) {
    const std::uint32_t address = byte.address & cpu::address_mask;
    expected_[address] = byte.value;
    mark(address, expected);
  }
}

std::uint8_t vector_bus::read_memory(std::uint32_t address) {
  const std::uint8_t state = state_[address];
  if ((state & (mapped | written)) == 0) {
    // The vectors list every byte the real part fetched, prefetch
    // overrun included, so a non-prefetching core reads a subset of
    // them. Reading outside that set means the address was computed
    // wrongly, and inventing a byte would turn a caught bug into a
    // mysterious one.
    if (faults_.size() < 8) {
      faults_.push_back("  read of " + hex(address, 5) +
                        ", which the vector does not map\n");
    }
    return 0xFF;
  }
  return value_[address];
}

void vector_bus::write_memory(std::uint32_t address, std::uint8_t value) {
  value_[address] = value;
  mark(address, written);
}

std::uint8_t vector_bus::read_port8(std::uint16_t port) {
  const std::vector<port_op>& script = test_->ports;
  if (port_index_ >= script.size()) {
    faults_.push_back("  port read of " + hex(port, 4) +
                      ", and the vector has no transaction left\n");
    return 0xFF;
  }
  const port_op& want = script[port_index_++];
  if (want.what != port_op::kind::read || want.port != port) {
    faults_.push_back(
        "  port op " + std::to_string(port_index_) + ": expected a " +
        (want.what == port_op::kind::read ? "read" : "write") + " of port " +
        hex(want.port, 4) + ", got a read of port " + hex(port, 4) + "\n");
  }
  return want.value;
}

void vector_bus::write_port8(std::uint16_t port, std::uint8_t value) {
  const std::vector<port_op>& script = test_->ports;
  if (port_index_ >= script.size()) {
    faults_.push_back("  port write of " + hex(value, 2) + " to " +
                      hex(port, 4) +
                      ", and the vector has no transaction left\n");
    return;
  }
  const port_op& want = script[port_index_++];
  if (want.what != port_op::kind::write || want.port != port ||
      want.value != value) {
    faults_.push_back(
        "  port op " + std::to_string(port_index_) + ": expected a " +
        (want.what == port_op::kind::read ? "read" : "write") + " of port " +
        hex(want.port, 4) + " (" + hex(want.value, 2) + "), got a write of " +
        hex(value, 2) + " to port " + hex(port, 4) + "\n");
  }
}

void vector_bus::check(std::string& report) const {
  int shown = 0;
  int hidden = 0;
  for (const std::uint32_t address : touched_) {
    const std::uint8_t state = state_[address];
    std::string line;
    if ((state & expected) != 0) {
      if ((state & (mapped | written)) == 0) {
        line = "  memory " + hex(address, 5) + "  expected " +
               hex(expected_[address], 2) + ", and nothing was written there\n";
      } else if (value_[address] != expected_[address]) {
        line = "  memory " + hex(address, 5) + "  expected " +
               hex(expected_[address], 2) + "  got " + hex(value_[address], 2) +
               "\n";
      }
    } else if ((state & written) != 0) {
      line = "  memory " + hex(address, 5) + "  written " +
             hex(value_[address], 2) +
             ", and the vector does not account for it\n";
    }
    if (line.empty()) {
      continue;
    }
    if (shown < 12) {
      report += line;
      ++shown;
    } else {
      ++hidden;
    }
  }
  if (hidden > 0) {
    report += "  (and " + std::to_string(hidden) + " more memory bytes)\n";
  }

  for (const std::string& fault : faults_) {
    report += fault;
  }

  if (test_ != nullptr && port_index_ < test_->ports.size()) {
    report += "  ports: " + std::to_string(test_->ports.size() - port_index_) +
              " of " + std::to_string(test_->ports.size()) +
              " transactions were never made\n";
  }
}

// --- The machine ------------------------------------------------------

vector_machine::vector_machine(const cpu::dispatch_table& table)
    : cpu_(bus_, nullptr, table) {}

std::string vector_machine::run(const vector_test& test) {
  bus_.begin(test);
  cpu_.reset();
  cpu_.regs() = test.before;

  std::string report;
  // A machine that stopped or hung never reached the state the vector
  // describes, so a register diff against it would be pages of noise
  // around the one line that matters.
  bool ran_to_completion = false;

  for (int steps = 1;; ++steps) {
    const cpu::step_status status = cpu_.step();
    if (status == cpu::step_status::ran) {
      ran_to_completion = true;
      break;
    }
    if (status == cpu::step_status::repeating) {
      if (steps < step_ceiling) {
        continue;
      }
      report += "  the instruction had not retired after " +
                std::to_string(step_ceiling) + " iterations\n";
      break;
    }
    if (status == cpu::step_status::stopped) {
      const cpu::stop_record& stop = cpu_.stop();
      report += "  stopped at " + hex(stop.cs, 4) + ":" + hex(stop.ip, 4) +
                " on opcode " + hex(stop.opcode, 2);
      if (stop.extension != cpu::no_extension) {
        report += " /" + std::to_string(stop.extension);
      }
      report += " — " + reason_text(stop.reason) + "\n";
      break;
    }
    report += "  the processor halted, which no vector asks it to\n";
    break;
  }

  if (ran_to_completion) {
    diff_registers(test.after, cpu_.regs(), report);
    bus_.check(report);
    if (report.empty()) {
      return report;
    }
  }

  std::string bytes;
  for (const std::uint8_t byte : test.bytes) {
    if (!bytes.empty()) {
      bytes += ' ';
    }
    bytes += hex(byte, 2);
  }
  return "test " + std::to_string(test.idx) + "  \"" + test.name +
         "\"  bytes " + bytes + "\n" + report;
}

}  // namespace amberfolio::conformance
