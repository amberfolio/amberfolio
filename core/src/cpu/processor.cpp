// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/cpu/processor.h"

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/decoder.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// Offsets are added modulo 65536, always. An effective address that
/// arithmetically exceeds a segment comes back round to the bottom of the
/// same segment; it never carries into the segment number.
[[nodiscard]] constexpr std::uint16_t wrap(unsigned offset) noexcept {
  return static_cast<std::uint16_t>(offset & 0xFFFFu);
}

/// A ModRM displacement byte is signed: mod 01 addresses reach 127 bytes
/// forward and 128 back.
[[nodiscard]] constexpr std::uint16_t sign_extend(std::uint8_t byte) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::int8_t>(byte));
}

}  // namespace

processor::processor(bus& machine_bus, diagnostics* log,
                     const dispatch_table& table) noexcept
    : bus_(&machine_bus), log_(log), table_(&table) {
  reset();
}

void processor::reset() noexcept {
  regs_ = registers{};
  regs_[sreg::cs] = 0xFFFF;
  regs_.flags = flag::reset_value;
  current_ = instruction{};
  stop_ = stop_record{};
  halted_ = false;
  repeating_ = false;

  // A RESET pin drops the NMI latch too, and nothing that was pending
  // across it means anything afterwards.
  nmi_latched_ = false;
  intr_asserted_ = false;
  intr_vector_ = 0;
  inhibited_ = false;
  trap_pending_ = false;
  suspended_ = false;
  resume_ip_ = 0;
}

// --- Memory -----------------------------------------------------------

std::uint8_t processor::read_byte(std::uint16_t segment, std::uint16_t offset) {
  return bus_->read_memory(physical_address(segment, offset));
}

void processor::write_byte(std::uint16_t segment, std::uint16_t offset,
                           std::uint8_t value) {
  bus_->write_memory(physical_address(segment, offset), value);
}

std::uint16_t processor::read_word(std::uint16_t segment,
                                   std::uint16_t offset) {
  const std::uint8_t low = read_byte(segment, offset);
  const std::uint8_t high = read_byte(segment, wrap(offset + 1u));
  return static_cast<std::uint16_t>(low | (high << 8u));
}

void processor::write_word(std::uint16_t segment, std::uint16_t offset,
                           std::uint16_t value) {
  write_byte(segment, offset, static_cast<std::uint8_t>(value));
  write_byte(segment, wrap(offset + 1u),
             static_cast<std::uint8_t>(value >> 8u));
}

std::uint16_t processor::read(width w, address at) {
  return w == width::byte ? read_byte(at.segment, at.offset)
                          : read_word(at.segment, at.offset);
}

void processor::write(width w, address at, std::uint16_t value) {
  if (w == width::byte) {
    write_byte(at.segment, at.offset, static_cast<std::uint8_t>(value));
  } else {
    write_word(at.segment, at.offset, value);
  }
}

// --- The stack --------------------------------------------------------

void processor::push_word(std::uint16_t value) {
  regs_[reg16::sp] = wrap(regs_[reg16::sp] - 2u);
  write_word(regs_[sreg::ss], regs_[reg16::sp], value);
}

std::uint16_t processor::pop_word() {
  const std::uint16_t value = read_word(regs_[sreg::ss], regs_[reg16::sp]);
  regs_[reg16::sp] = wrap(regs_[reg16::sp] + 2u);
  return value;
}

// --- The instruction stream -------------------------------------------

std::uint8_t processor::fetch_byte() {
  const std::uint8_t byte = read_byte(regs_[sreg::cs], regs_.ip);
  regs_.ip = wrap(regs_.ip + 1u);
  return byte;
}

std::uint16_t processor::fetch_word() {
  const std::uint8_t low = fetch_byte();
  const std::uint8_t high = fetch_byte();
  return static_cast<std::uint16_t>(low | (high << 8u));
}

// --- Operands ---------------------------------------------------------

std::uint16_t processor::read_rm(width w) {
  const cpu::modrm& m = current_.modrm;
  if (m.names_a_register()) {
    return w == width::byte ? std::uint16_t{regs_.get(static_cast<reg8>(m.rm))}
                            : regs_[static_cast<reg16>(m.rm)];
  }
  return read(w, current_.ea);
}

void processor::write_rm(width w, std::uint16_t value) {
  const cpu::modrm& m = current_.modrm;
  if (m.names_a_register()) {
    if (w == width::byte) {
      regs_.set(static_cast<reg8>(m.rm), static_cast<std::uint8_t>(value));
    } else {
      regs_[static_cast<reg16>(m.rm)] = value;
    }
    return;
  }
  write(w, current_.ea, value);
}

std::uint16_t processor::read_reg(width w) {
  const std::uint8_t field = current_.modrm.reg;
  return w == width::byte ? std::uint16_t{regs_.get(static_cast<reg8>(field))}
                          : regs_[static_cast<reg16>(field)];
}

void processor::write_reg(width w, std::uint16_t value) {
  const std::uint8_t field = current_.modrm.reg;
  if (w == width::byte) {
    regs_.set(static_cast<reg8>(field), static_cast<std::uint8_t>(value));
  } else {
    regs_[static_cast<reg16>(field)] = value;
  }
}

// --- Decoding ---------------------------------------------------------

std::uint8_t processor::fetch_opcode() {
  std::uint8_t byte = fetch_byte();

  while (is_prefix(byte)) {
    prefix_state& p = current_.prefixes;
    p.last_prefix_ip = wrap(regs_.ip - 1u);

    if (p.count == prefix_limit) {
      // The record names the prefix byte it gave up on rather than an
      // opcode, because it never reached one.
      current_.opcode = byte;
      stop_with(stop_reason::prefix_chain_too_long, no_extension);
      return byte;
    }
    ++p.count;

    switch (byte) {
      case 0xF0:
      case 0xF1:
        p.lock = true;
        break;
      case 0xF2:
        p.rep = repeat::repne;
        break;
      case 0xF3:
        p.rep = repeat::repe;
        break;
      default:
        // A segment override, then: the four bytes is_prefix accepts that
        // are not the three above.
        p.has_segment_override = true;
        p.segment_override = override_segment(byte);
        break;
    }

    byte = fetch_byte();
  }

  return byte;
}

address processor::effective_address(const cpu::modrm& m) {
  const std::uint16_t bx = regs_[reg16::bx];
  const std::uint16_t bp = regs_[reg16::bp];
  const std::uint16_t si = regs_[reg16::si];
  const std::uint16_t di = regs_[reg16::di];

  std::uint16_t offset = 0;

  // BP is the register that changes the default segment: an address built
  // on it is a stack-frame address, so it defaults to SS while everything
  // else defaults to DS. The exception is mod 00 rm 110, whose encoding
  // slot BP would otherwise occupy — that form is a bare 16-bit address,
  // and it is a DS one.
  bool based_on_bp = false;

  switch (m.rm) {
    case 0:
      offset = wrap(bx + si);
      break;
    case 1:
      offset = wrap(bx + di);
      break;
    case 2:
      offset = wrap(bp + si);
      based_on_bp = true;
      break;
    case 3:
      offset = wrap(bp + di);
      based_on_bp = true;
      break;
    case 4:
      offset = si;
      break;
    case 5:
      offset = di;
      break;
    case 6:
      if (m.mod == 0) {
        offset = fetch_word();  // direct address; no BP, no displacement
      } else {
        offset = bp;
        based_on_bp = true;
      }
      break;
    default:  // 7
      offset = bx;
      break;
  }

  if (m.mod == 1) {
    offset = wrap(offset + sign_extend(fetch_byte()));
  } else if (m.mod == 2) {
    offset = wrap(offset + fetch_word());
  }

  const prefix_state& p = current_.prefixes;
  const sreg segment = p.has_segment_override
                           ? p.segment_override
                           : (based_on_bp ? sreg::ss : sreg::ds);

  return {.segment = regs_[segment], .offset = offset};
}

void processor::decode_operands() {
  current_.modrm = split_modrm(fetch_byte());
  current_.modrm_present = true;

  if (!current_.modrm.names_a_register()) {
    current_.ea = effective_address(current_.modrm);
  }
}

// --- Stopping ---------------------------------------------------------

step_status processor::stop_with(stop_reason reason, std::uint8_t extension) {
  regs_.ip = current_.start_ip;
  stop_ = stop_record{.reason = reason,
                      .opcode = current_.opcode,
                      .extension = extension,
                      .cs = regs_[sreg::cs],
                      .ip = current_.start_ip};
  if (log_ != nullptr) {
    log_->report(stop_);
  }
  return step_status::stopped;
}

// --- The step loop ----------------------------------------------------

step_status processor::step() {
  // First, and before anything is fetched: a stopped processor touches no
  // bus, and the state it stopped in is the one a human is looking at.
  if (stopped()) {
    return step_status::stopped;
  }

  // The instruction boundary. Everything about *when* an interrupt is
  // taken is in service_interrupt() and in interrupts.h; what matters
  // here is that it happens before the fetch, so an interrupt the machine
  // raised between steps is taken before the next instruction rather than
  // after it, and that it is the only thing that ends a halt.
  if (service_interrupt()) {
    return step_status::serviced;
  }

  if (halted_) {
    return step_status::halted;
  }

  current_ = instruction{};
  current_.start_ip = regs_.ip;
  // Cleared here rather than after the handler, so that a handler which
  // sets it is the only thing that can make this step report `repeating`.
  repeating_ = false;
  suspended_ = false;

  // TF is sampled at the *start* of the instruction and the trap it owes
  // is delivered at the boundary after it. That one line is the whole of
  // why POPF and IRET can turn single-stepping back on without trapping
  // on themselves — see interrupts.h.
  const bool trap_armed = regs_.flag_set(flag::tf);

  current_.opcode = fetch_opcode();
  if (stopped()) {
    // fetch_opcode gave up on the prefix run. It has already rewound IP
    // and reported; `current_.opcode` is whatever byte it stopped on and
    // is not an opcode.
    return step_status::stopped;
  }

  if (has_modrm(current_.opcode)) {
    decode_operands();
  }

  // The reg field is the instruction's identity for a group opcode, and
  // an operand for everything else; `find` knows which, and the stop
  // record wants the same distinction so it can say *which* of a group's
  // eight instructions was missing.
  const bool is_group = group_slot(current_.opcode) != not_a_group;
  const std::uint8_t extension = is_group ? current_.modrm.reg : no_extension;

  const handler run = table_->find(current_.opcode, extension);
  if (run == nullptr) {
    return stop_with(stop_reason::unimplemented_opcode, extension);
  }

  run(*this);

  // An instruction that ran owes a trap if it began with TF set — even if
  // it was an INT, whose own delivery has since cleared TF. That is why
  // tracing over an `INT 21h` lands a debugger on the first instruction
  // of the handler rather than on the instruction after the INT.
  if (trap_armed) {
    trap_pending_ = true;
  }

  if (!repeating_) {
    return step_status::ran;
  }

  // The instruction has not retired. The handler has rewound IP to the
  // instruction's first byte so the next step re-enters it whole, but an
  // interrupt taken at this boundary does not resume there — the 8086
  // backs up only as far as the last prefix, and the earlier ones are
  // lost. interrupts.h has the two encodings that make that visible.
  suspended_ = true;
  resume_ip_ =
      current_.prefixes.count > 0 ? current_.prefixes.last_prefix_ip : regs_.ip;
  return step_status::repeating;
}

}  // namespace amberfolio::cpu
