// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/report.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/trace.h"

namespace amberfolio::machine {
namespace {

/// A bounded writer over the caller's buffer.
///
/// <cstdio> is not used here for the reason the hosts state at their own
/// tops and one more besides: `snprintf` would be one call per field with
/// a running offset the caller has to get right every time, and the
/// truncation rule would then live in four places instead of one. This is
/// a dozen lines and it cannot overrun: every `put` checks, and the
/// terminator is written by `finish()` from a position that is always
/// inside the buffer.
class writer {
 public:
  explicit writer(std::span<char> out) noexcept : out_(out) {}

  void put(char c) noexcept {
    // One short of the size, always: the last byte belongs to the
    // terminator, so that a buffer that filled up is still a C string.
    if (out_.empty() || used_ + 1 >= out_.size()) {
      return;
    }
    out_[used_++] = c;
  }

  void text(const char* s) noexcept {
    for (const char* c = s; *c != '\0'; ++c) {
      put(*c);
    }
  }

  /// `digits` hexadecimal characters, upper case, zero-padded — the
  /// spelling every address and register in this codebase is written in.
  void hex(std::uint32_t value, unsigned digits) noexcept {
    constexpr std::array<char, 16> table = {'0', '1', '2', '3', '4', '5',
                                            '6', '7', '8', '9', 'A', 'B',
                                            'C', 'D', 'E', 'F'};
    for (unsigned i = digits; i > 0; --i) {
      put(table[(value >> ((i - 1) * 4u)) & 0x0Fu]);
    }
  }

  void number(std::uint64_t value) noexcept {
    // Twenty digits is the widest a 64-bit value can be, so the scratch
    // array cannot overflow whatever the caller passes.
    std::array<char, 20> digits{};
    std::size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10u));
      value /= 10u;
    } while (value != 0);
    while (count > 0) {
      put(digits[--count]);
    }
  }

  /// `SSSS:OOOO`, the way a segmented address is written everywhere else
  /// in the tree.
  void far_address(std::uint16_t segment, std::uint16_t offset) noexcept {
    hex(segment, 4);
    put(':');
    hex(offset, 4);
  }

  void line_start() noexcept { text("amberfolio: stop "); }

  void end_line() noexcept { put('\n'); }

  [[nodiscard]] std::size_t finish() noexcept {
    if (!out_.empty()) {
      out_[used_] = '\0';
    }
    return used_;
  }

 private:
  std::span<char> out_;
  std::size_t used_{};
};

/// The `next=` line: the one thing to widen, named by the machine.
///
/// Only the stops a program can *cause* get one. A conflicting claim is a
/// mistake in how the machine was wired (diagnostics.h) and a program
/// that exited has asked for nothing; neither belongs on a worklist of
/// services to implement, and printing a line for them would put two
/// non-entries at the top of every M3 boot log.
void write_worklist(writer& out, const machine& box) {
  const stop_record& stop = box.stop();
  const service_call* call = box.last_service_call();

  switch (stop.reason) {
    case stop_reason::unimplemented_service:
    case stop_reason::unsupported_request:
      if (call == nullptr) {
        return;
      }
      out.line_start();
      out.text("next=INT ");
      out.hex(call->vector, 2);
      out.text("h AH=");
      out.hex(call->function(), 2);
      out.text("h AL=");
      out.hex(static_cast<std::uint8_t>(call->ax & 0xFFu), 2);
      out.text("h");
      out.end_line();
      return;

    case stop_reason::unimplemented_device: {
      out.line_start();
      out.text("next=device ");
      // A device's fault names a port or a physical address, and there is
      // nothing in the record that distinguishes them — device.h leaves
      // that to the device. Printed as the number it is, five digits
      // wide, so a port (0000-FFFF) and an address are both legible and
      // neither is dressed up as the other.
      out.hex(stop.at, 5);
      const device_stop* fault = box.last_device_stop();
      if (fault != nullptr) {
        out.text(" detail=");
        out.hex(fault->detail, 2);
      }
      out.end_line();
      return;
    }

    case stop_reason::processor: {
      const cpu::stop_record& refused = box.processor().stop();
      out.line_start();
      out.text("next=opcode ");
      out.hex(refused.opcode, 2);
      if (refused.extension != cpu::no_extension) {
        out.text(" ext=");
        out.hex(refused.extension, 1);
      }
      out.end_line();
      return;
    }

    case stop_reason::none:
    case stop_reason::conflicting_claim:
    case stop_reason::program_exited:
      return;
  }
}

}  // namespace

const char* stop_reason_name(stop_reason reason) noexcept {
  switch (reason) {
    case stop_reason::none:
      return "none";
    case stop_reason::processor:
      return "processor";
    case stop_reason::unimplemented_service:
      return "unimplemented_service";
    case stop_reason::conflicting_claim:
      return "conflicting_claim";
    case stop_reason::unimplemented_device:
      return "unimplemented_device";
    case stop_reason::program_exited:
      return "program_exited";
    case stop_reason::unsupported_request:
      return "unsupported_request";
  }
  return "unknown";
}

const char* run_end_name(run_end how) noexcept {
  switch (how) {
    case run_end::stopped:
      return "stopped";
    case run_end::step_budget:
      return "step_budget";
    case run_end::tick_budget:
      return "tick_budget";
    case run_end::host_quit:
      return "host_quit";
  }
  return "unknown";
}

const char* notice_kind_name(notice_kind what) noexcept {
  switch (what) {
    case notice_kind::unmapped_memory_read:
      return "unmapped_memory_read";
    case notice_kind::unmapped_memory_write:
      return "unmapped_memory_write";
    case notice_kind::rom_write:
      return "rom_write";
    case notice_kind::unclaimed_port_read:
      return "unclaimed_port_read";
    case notice_kind::unclaimed_port_write:
      return "unclaimed_port_write";
    case notice_kind::video_write_before_mode_set:
      return "video_write_before_mode_set";
  }
  return "unknown";
}

const char* cpu_stop_reason_name(cpu::stop_reason reason) noexcept {
  switch (reason) {
    case cpu::stop_reason::none:
      return "none";
    case cpu::stop_reason::unimplemented_opcode:
      return "unimplemented_opcode";
    case cpu::stop_reason::prefix_chain_too_long:
      return "prefix_chain_too_long";
  }
  return "unknown";
}

std::size_t format_stop_report(const machine& box, run_end how,
                               std::span<char> out) {
  writer w(out);

  const stop_record& stop = box.stop();
  const cpu::registers& regs = box.processor().regs();

  // The headline. `reason=` is the machine's own word when the machine is
  // what ended the run, and the host's when it is not — one field, so a
  // reader (and a grep) has one place to look for "why did this end".
  w.line_start();
  w.text("reason=");
  w.text(how == run_end::stopped ? stop_reason_name(stop.reason)
                                 : run_end_name(how));
  w.text(" steps=");
  w.number(box.steps());
  w.text(" ticks=");
  w.number(box.time());
  w.text(" frames=");
  w.number(box.display().generation());
  w.text(" cs=");
  w.hex(regs[cpu::sreg::cs], 4);
  w.text(" ip=");
  w.hex(regs.ip, 4);
  w.text(" at=");
  w.hex(stop.at, 5);
  w.end_line();

  // The service call, when there has been one. Printed on every report
  // rather than only on a service stop: on a hang it is the last thing
  // the program asked its operating system for, which is very often the
  // whole clue, and it costs one line.
  if (const service_call* call = box.last_service_call(); call != nullptr) {
    w.line_start();
    w.text("call=INT");
    w.hex(call->vector, 2);
    w.text(" ah=");
    w.hex(call->function(), 2);
    w.text(" al=");
    w.hex(static_cast<std::uint8_t>(call->ax & 0xFFu), 2);
    w.text(" ax=");
    w.hex(call->ax, 4);
    w.text(" from=");
    w.far_address(call->caller_cs, call->caller_ip);
    w.text(" outcome=");
    w.text(call->outcome == service_outcome::handled ? "handled"
                                                     : "unimplemented");
    w.end_line();
  }

  if (const device_stop* fault = box.last_device_stop(); fault != nullptr) {
    w.line_start();
    w.text("device=");
    w.hex(fault->at, 5);
    w.text(" detail=");
    w.hex(fault->detail, 2);
    w.text(" from=");
    w.far_address(fault->cs, fault->ip);
    w.end_line();
  }

  if (stop.reason == stop_reason::processor) {
    const cpu::stop_record& refused = box.processor().stop();
    w.line_start();
    w.text("cpu=");
    w.text(cpu_stop_reason_name(refused.reason));
    w.text(" opcode=");
    w.hex(refused.opcode, 2);
    w.text(" ext=");
    w.hex(refused.extension, 2);
    w.text(" from=");
    w.far_address(refused.cs, refused.ip);
    w.end_line();
  }

  if (stop.reason == stop_reason::program_exited) {
    w.line_start();
    w.text("exit=");
    w.number(stop.exit_code);
    w.end_line();
  }

  write_worklist(w, box);

  return w.finish();
}

std::size_t format_trace_report(const machine& box, std::span<char> out) {
  writer w(out);
  const trace_ring& ring = box.trace();

  if (!ring.enabled() && ring.steps_seen() == 0) {
    w.line_start();
    w.text("trace=off\n");
    return w.finish();
  }

  w.line_start();
  w.text("trace=on steps_seen=");
  w.number(ring.steps_seen());
  w.text(" kept=");
  w.number(ring.step_count());
  w.text(" calls_seen=");
  w.number(ring.calls_seen());
  w.text(" kept=");
  w.number(ring.call_count());
  w.end_line();

  // Calls first. There are fewer of them, they carry the most meaning per
  // line, and a reader who only reads the top of the block should be
  // reading those rather than three hundred addresses.
  for (std::size_t i = 0; i < ring.call_count(); ++i) {
    const service_call call = ring.call_at(i);
    w.line_start();
    w.text("trace call=INT");
    w.hex(call.vector, 2);
    w.text(" ax=");
    w.hex(call.ax, 4);
    w.text(" from=");
    w.far_address(call.caller_cs, call.caller_ip);
    w.text(" outcome=");
    w.text(call.outcome == service_outcome::handled ? "handled"
                                                    : "unimplemented");
    w.end_line();
  }

  // Then the steps, oldest first, numbered by the absolute step they
  // were: a reader comparing two runs wants to know that this is step
  // 8,411,750 and not "the fiftieth line of the block".
  const std::uint64_t oldest = ring.steps_seen() - ring.step_count();
  for (std::size_t i = 0; i < ring.step_count(); ++i) {
    const trace_step where = ring.step_at(i);
    w.line_start();
    w.text("trace step=");
    w.number(oldest + i);
    w.text(" at=");
    w.far_address(where.cs, where.ip);
    w.end_line();
  }

  return w.finish();
}

}  // namespace amberfolio::machine
