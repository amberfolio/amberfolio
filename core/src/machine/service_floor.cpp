// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/service_floor.h"

#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/font.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/port_map.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] std::uint16_t low_half(std::uint32_t value) noexcept {
  return static_cast<std::uint16_t>(value & 0xFFFFu);
}

[[nodiscard]] std::uint16_t high_half(std::uint32_t value) noexcept {
  return static_cast<std::uint16_t>(value >> 16u);
}

[[nodiscard]] std::uint16_t offset_past(std::uint16_t at,
                                        std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(at + by);
}

// --- The default handlers ---------------------------------------------

/// A vector whose entire service is its IRET.
///
/// Not the same thing as an unimplemented one, and the distinction is the
/// point: INT 1Ch does nothing on a real machine either, so doing nothing
/// here is the correct answer rather than a missing one. It exists as a
/// handler rather than as a null slot so that the trace channel shows the
/// call — a program hanging a routine off 1Ch wants to see the tick
/// arriving even when nothing is hooked yet.
void nothing_to_do(service_floor& /*floor*/, std::uint8_t /*vector*/) {}

/// The default INT 08h, first half: the tick count, then the user vector.
void timer_tick(service_floor& floor, std::uint8_t /*vector*/) {
  cpu::processor& cpu = floor.box().processor();

  // Through the bus, not through ram(): the BDA is ordinary memory that
  // the program reads and writes with ordinary instructions, and a
  // handler written in 8086 would touch it exactly like this. The back
  // door is for the machine laying memory out, not for the machine
  // running.
  const std::uint16_t low = cpu.read_word(bda::segment, bda::timer_ticks);
  const std::uint16_t high =
      cpu.read_word(bda::segment, offset_past(bda::timer_ticks, 2));

  std::uint32_t ticks = (static_cast<std::uint32_t>(high) << 16u) | low;
  ++ticks;
  if (ticks >= bda::ticks_per_day) {
    // Midnight. The count returns to zero and the rollover byte counts
    // the day, which is the only record that it happened: a program that
    // reads the tick count twice across midnight would otherwise see time
    // run backwards with nothing to explain it.
    ticks = 0;
    const std::uint8_t days = cpu.read_byte(bda::segment, bda::timer_rollover);
    cpu.write_byte(bda::segment, bda::timer_rollover,
                   static_cast<std::uint8_t>(days + 1u));
  }

  cpu.write_word(bda::segment, bda::timer_ticks, low_half(ticks));
  cpu.write_word(bda::segment, offset_past(bda::timer_ticks, 2),
                 high_half(ticks));

  // The user tick, and it is a real interrupt: pushed through
  // `deliver_interrupt`, so it goes wherever the IVT entry for 1Ch says,
  // which is the hook if a program has installed one and our do-nothing
  // stub if it has not. Nothing here looks at whether it was hooked.
  //
  // IP is moved to the continuation first, because delivery pushes IP as
  // the caller leaves it (cpu/interrupts.h). That is what makes the
  // second half of this handler the return address, exactly as the
  // instruction after an `INT 1Ch` would be.
  cpu.regs().ip = service::continuation_offset(service::timer_continuation);
  cpu.deliver_interrupt(service::user_tick_vector);
}

/// The default INT 08h, second half: what runs when INT 1Ch returns.
void timer_tick_done(service_floor& floor, std::uint8_t /*vector*/) {
  // A real write to the real port. There is no 8259 in the machine yet
  // (M2-D1, #46), so the port map finds nothing there and the machine
  // says so once — which is the true state of this machine and better
  // than an EOI quietly swallowed. When the controller arrives and claims
  // port 20h, this line starts working and nothing here changes.
  //
  // After the user tick rather than before it, which is the order this
  // milestone's design of record states. It is observable only to a 1Ch
  // handler that re-enables interrupts and runs long enough for a second
  // tick to be due.
  floor.box().write_port8(pic::master_command_port, pic::end_of_interrupt);
}

}  // namespace

service_floor::service_floor(machine& box, diagnostics* log) noexcept
    : box_(&box), log_(log) {
  // Every vector stub knows which vector it is, so that a service with no
  // body can still name itself when it refuses. Nothing reads `box` here:
  // this runs while the machine is still being constructed.
  for (unsigned v = 0; v < service::vector_stubs; ++v) {
    stubs_[v].vector = static_cast<std::uint8_t>(v);
  }

  // The one service this layer provides itself, and the proof that the
  // mechanism works: the system timer, its user tick vector, and the
  // continuation that joins them.
  provide(service::user_tick_vector, &nothing_to_do);
  provide(service::timer_vector, &timer_tick);
  provide_continuation(service::timer_continuation, service::timer_vector,
                       &timer_tick_done);
}

void service_floor::provide(std::uint8_t vector, service_handler handler) {
  stub& slot = stubs_[service::stub_index(service::stub_offset(vector))];
  slot.handler = handler;
  slot.vector = vector;
}

std::uint16_t service_floor::provide_continuation(unsigned slot,
                                                  std::uint8_t vector,
                                                  service_handler handler) {
  if (slot >= service::continuation_stubs) {
    return 0;
  }

  const std::uint16_t at = service::continuation_offset(slot);
  stub& entry = stubs_[service::stub_index(at)];
  entry.handler = handler;
  entry.vector = vector;
  return at;
}

bool service_floor::enabled() const noexcept {
  // The PC map is the one with a BIOS region in it. The flat layout is
  // not a machine that ever existed (memory_map.h) and has no room for a
  // service floor that would not be sitting in the program's own RAM.
  return box_->memory().layout() == memory_layout::pc;
}

void service_floor::reset() {
  if (!enabled()) {
    return;
  }

  const std::span<std::uint8_t> ram = box_->memory().ram();

  // The vector table: 256 far pointers at 0000:0000, every one of them
  // into the BIOS region. Written low byte first, which is how the
  // machine reads them back — the entry a hook overwrites has to be
  // byte-for-byte the entry a program would write itself.
  for (unsigned v = 0; v < service::vector_stubs; ++v) {
    const std::uint32_t entry = cpu::physical_address(
        cpu::vector_table_segment,
        cpu::vector_table_offset(static_cast<std::uint8_t>(v)));
    const std::uint16_t at = service::stub_offset(static_cast<std::uint8_t>(v));

    ram[entry] = static_cast<std::uint8_t>(at);
    ram[entry + 1] = static_cast<std::uint8_t>(at >> 8u);
    ram[entry + 2] = static_cast<std::uint8_t>(service::stub_segment);
    ram[entry + 3] = static_cast<std::uint8_t>(service::stub_segment >> 8u);
  }

  // The stubs themselves. One byte each, and the same byte each: the
  // handler is what makes them different, and the memory only has to
  // return.
  for (unsigned slot = 0; slot < service::stub_count; ++slot) {
    const std::uint16_t at =
        slot < service::vector_stubs
            ? service::stub_offset(static_cast<std::uint8_t>(slot))
            : service::continuation_offset(slot - service::vector_stubs);
    ram[cpu::physical_address(service::stub_segment, at)] =
        service::iret_opcode;
  }

  // The character generator, and the two vectors that point at it.
  //
  // A real self test leaves a font in ROM and the addresses of it in 1Fh
  // and 43h; so does this one, for the reasons font.h argues. These two
  // are written *after* the loop above, which has just aimed all 256
  // vectors at stubs — they are not entry points and never were
  // (font.h), so a stub is exactly the wrong thing to leave in them.
  const std::span<const std::uint8_t> glyphs = font::glyphs();
  const std::uint32_t font_at =
      cpu::physical_address(service::stub_segment, service::font_offset);
  for (std::uint16_t i = 0; i < font::table_bytes; ++i) {
    ram[font_at + i] = glyphs[i];
  }

  const auto point_at = [&ram](std::uint8_t vector, std::uint16_t offset) {
    const std::uint32_t entry = cpu::physical_address(
        cpu::vector_table_segment, cpu::vector_table_offset(vector));
    ram[entry] = static_cast<std::uint8_t>(offset);
    ram[entry + 1] = static_cast<std::uint8_t>(offset >> 8u);
    ram[entry + 2] = static_cast<std::uint8_t>(service::stub_segment);
    ram[entry + 3] = static_cast<std::uint8_t>(service::stub_segment >> 8u);
  };
  point_at(font::generator_vector, service::font_offset);
  point_at(
      font::high_half_vector,
      static_cast<std::uint16_t>(service::font_offset +
                                 font::high_half_first * font::glyph_height));

  // The BDA: cleared, then the fields this machine maintains. Clearing
  // first is what the self test does, and it is what makes a warm boot
  // start from the same BDA a cold one does — the RESET line leaves RAM
  // alone, so without this the previous run's tick count would survive
  // into the next one.
  const std::uint32_t area = cpu::physical_address(bda::segment, 0);
  for (std::uint16_t i = 0; i < bda::size; ++i) {
    ram[area + i] = 0;
  }

  const auto kb = static_cast<std::uint16_t>(conventional_ram_size / 1024u);
  ram[area + bda::memory_size_kb] = static_cast<std::uint8_t>(kb);
  ram[area + bda::memory_size_kb + 1] = static_cast<std::uint8_t>(kb >> 8u);

  // The video block, describing the one mode this machine can display —
  // see the `bda` namespace in service_floor.h for why it is written at
  // all before a program has asked for a mode.
  ram[area + bda::video_mode] = bda::power_on_video_mode;
  ram[area + bda::video_columns] =
      static_cast<std::uint8_t>(bda::power_on_video_columns);
  ram[area + bda::video_columns + 1] =
      static_cast<std::uint8_t>(bda::power_on_video_columns >> 8u);
  ram[area + bda::video_rows_minus_one] = bda::power_on_video_rows_minus_one;
  ram[area + bda::character_points] =
      static_cast<std::uint8_t>(bda::power_on_character_points);
  ram[area + bda::character_points + 1] =
      static_cast<std::uint8_t>(bda::power_on_character_points >> 8u);

  // The keyboard buffer starts empty at its own first slot, not at zero.
  // A real BIOS's self test does the same, and 40:1A/40:1C are offsets a
  // program may walk directly (keyboard.h, M2-D8) — zero is not an offset
  // any real machine's pointers ever hold, only 001Eh through 003Eh are.
  ram[area + bda::keyboard_buffer_head] =
      static_cast<std::uint8_t>(bda::keyboard_buffer);
  ram[area + bda::keyboard_buffer_head + 1] =
      static_cast<std::uint8_t>(bda::keyboard_buffer >> 8u);
  ram[area + bda::keyboard_buffer_tail] =
      static_cast<std::uint8_t>(bda::keyboard_buffer);
  ram[area + bda::keyboard_buffer_tail + 1] =
      static_cast<std::uint8_t>(bda::keyboard_buffer >> 8u);

  // And then the hardware, which is the other half of what a self test
  // is for — see `program_hardware()`.
  program_hardware();
}

void service_floor::program_hardware() {
  // Through the bus, as OUT instructions, because that is what a ROM's
  // self test is: a table of them. `machine::reset()` has already reset
  // every attached device and rebased the clock by the time this runs, so
  // a deadline armed here is armed against the run that is starting.
  machine& box = *box_;

  // "If it is there." A real self test cannot ask its own bus what is
  // plugged into it, but this floor already knows things about its own
  // machine that no ROM could — `enabled()` reads the memory layout — and
  // the alternative is worse in both directions: a machine wired without
  // a timer would collect open-bus notices at every reset for hardware
  // nobody claimed was present, and the notices would say "the BIOS
  // touched a port nothing answers", which is a report about this
  // function rather than about the program under test.
  const auto present = [&box](std::uint16_t port) {
    return box.ports().owner(port) != nullptr;
  };

  // The interval timer, channel 0: mode 3, both bytes, binary, and a
  // divisor of zero — which is 65536 (pit.h), giving 1193182/65536 =
  // 18.2065 Hz, the rate every DOS-era program means when it reads the
  // tick count at 40:6C. Channels 1 and 2 are left alone: channel 1 was
  // DRAM refresh on a real PC and this machine has no DRAM to refresh,
  // and channel 2 belongs to whoever programs the speaker next.
  if (present(pit_control_port)) {
    box.write_port8(pit_control_port, post::timer_control);
    box.write_port8(pit_channel0_port, 0x00);
    box.write_port8(pit_channel0_port, 0x00);
  }

  // The interrupt controller: the stock sequence pic.h documents, then a
  // mask that leaves exactly the one line this machine has wired open.
  // Without this the PIT counts and raises IRQ0 into a controller that
  // has no vector base to deliver it with, and a program waiting on the
  // tick count waits forever — which is precisely what M3's first boot
  // did (#87, #88), one instruction short of a stop the report could
  // name.
  if (present(pic::master_command_port)) {
    box.write_port8(pic::master_command_port, pic::icw1_edge_cascade_icw4);
    box.write_port8(pic::data_port, pic::expected_vector_base);
    box.write_port8(pic::data_port, post::cascade_on_irq2);
    box.write_port8(pic::data_port, pic::icw4_8086_mode);
    box.write_port8(pic::data_port, post::interrupt_mask);
  }
}

void service_floor::report_file(file_action what, const dos_path& path,
                                std::uint16_t handle, vfs_error error) {
  if (log_ == nullptr && !box_->trace().enabled()) {
    // The caller's frame is a stack read; skipping it when nobody is
    // listening keeps a run with a sink and a run without one the same
    // run, which is the rule `call()` above states at length. The trace
    // ring counts as listening (#121): a machine with tracing on and no
    // sink is a real configuration, and the ring is the half of `--trace`
    // that a reader reads after the fact.
    return;
  }
  const file_event event{.what = what,
                         .path = path,
                         .handle = handle,
                         .error = error,
                         .caller_cs = caller_cs(),
                         .caller_ip = caller_ip()};
  // Recorded before it is reported, the same order — and for the same
  // reason — as `call()` below.
  box_->note_file_event(event);
  if (log_ != nullptr) {
    log_->report(event);
  }
}

service_outcome service_floor::call(unsigned slot) {
  const stub& entry = stubs_[slot];
  const service_outcome outcome = entry.handler == nullptr
                                      ? service_outcome::unimplemented
                                      : service_outcome::handled;

  // Built before the branch and whether or not anyone is listening: it
  // reads the caller's stack, and a machine whose bus cycles depended on
  // whether a sink was attached would not be the same machine twice.
  const service_call record{.vector = entry.vector,
                            .ax = box_->processor().regs()[cpu::reg16::ax],
                            .caller_cs = caller_cs(),
                            .caller_ip = caller_ip(),
                            .outcome = outcome};
  // The machine keeps it whether or not a sink does: it is what a stop
  // report needs to name the service to widen next (machine.h's
  // `last_service_call()`), and it is what feeds the trace ring when one
  // is running (trace.h). Before the sink, so that the two orderings a
  // reader might infer — "recorded, then reported" — is the one that is
  // true.
  box_->note_service_call(record);
  if (log_ != nullptr) {
    log_->report(record);
  }

  if (outcome == service_outcome::unimplemented) {
    return outcome;
  }

  entry.handler(*this, entry.vector);
  return service_outcome::handled;
}

std::uint16_t service_floor::frame_word(std::uint16_t at) {
  cpu::processor& cpu = box_->processor();
  return cpu.read_word(cpu.regs()[cpu::sreg::ss],
                       offset_past(cpu.regs()[cpu::reg16::sp], at));
}

void service_floor::set_frame_word(std::uint16_t at, std::uint16_t value) {
  cpu::processor& cpu = box_->processor();
  cpu.write_word(cpu.regs()[cpu::sreg::ss],
                 offset_past(cpu.regs()[cpu::reg16::sp], at), value);
}

std::uint16_t service_floor::caller_ip() {
  return frame_word(service::frame::return_ip);
}

std::uint16_t service_floor::caller_cs() {
  return frame_word(service::frame::return_cs);
}

std::uint16_t service_floor::caller_flags() {
  return frame_word(service::frame::flags);
}

void service_floor::set_caller_flags(std::uint16_t value) {
  set_frame_word(service::frame::flags, value);
}

void service_floor::set_caller_carry(bool failed) {
  set_caller_flags(cpu::flag::with(caller_flags(), cpu::flag::cf, failed));
}

}  // namespace amberfolio::machine
