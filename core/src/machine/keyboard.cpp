// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/keyboard.h"

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/state.h"

namespace amberfolio::machine {
namespace {

// --- The BDA buffer, addressed the way a program addresses it ----------
//
// Every one of these goes through `cpu::processor::read_byte`/`write_byte`
// rather than `memory_map::ram()`: this is the machine acting as the
// keyboard's own firmware would, touching memory a program can also touch,
// not the machine laying memory out (service_floor.cpp's `reset()` is the
// one place that back door belongs).

[[nodiscard]] bool kb_buffer_empty(cpu::processor& cpu) {
  return cpu.read_word(bda::segment, bda::keyboard_buffer_head) ==
         cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
}

/// The head word, without removing it — what AH=01h previews.
[[nodiscard]] std::uint16_t kb_buffer_peek(cpu::processor& cpu) {
  const std::uint16_t at =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  return cpu.read_word(bda::segment, at);
}

/// Remove and answer the head word. Caller must have checked non-empty.
[[nodiscard]] std::uint16_t kb_buffer_pop(cpu::processor& cpu) {
  const std::uint16_t at =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  const std::uint16_t word = cpu.read_word(bda::segment, at);

  auto next = static_cast<std::uint16_t>(at + 2u);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  cpu.write_word(bda::segment, bda::keyboard_buffer_head, next);
  return word;
}

/// Add `word` at the tail. False, and nothing written, if the buffer is
/// full — the keystroke is dropped, exactly as the BIOS's own buffer
/// drops one (service_floor.h's `bda` namespace has the reason there is
/// always one free slot).
bool kb_buffer_push(cpu::processor& cpu, std::uint16_t word) {
  const std::uint16_t tail =
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
  auto next = static_cast<std::uint16_t>(tail + 2u);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  if (next == cpu.read_word(bda::segment, bda::keyboard_buffer_head)) {
    return false;
  }

  cpu.write_word(bda::segment, tail, word);
  cpu.write_word(bda::segment, bda::keyboard_buffer_tail, next);
  return true;
}

// --- The shift-flag byte, 40:17 -----------------------------------------

[[nodiscard]] bool shift_flag(cpu::processor& cpu, std::uint8_t mask) {
  return (cpu.read_byte(bda::segment, bda::keyboard_shift_flags) & mask) != 0;
}

void set_shift_flag(cpu::processor& cpu, std::uint8_t mask, bool value) {
  const std::uint8_t flags =
      cpu.read_byte(bda::segment, bda::keyboard_shift_flags);
  cpu.write_byte(bda::segment, bda::keyboard_shift_flags,
                 value ? static_cast<std::uint8_t>(flags | mask)
                       : static_cast<std::uint8_t>(flags & ~mask));
}

/// A lock key (Caps/Num/Scroll): flip the flag on a fresh press, do
/// nothing on an auto-repeated one, and clear the "held" bookkeeping on
/// release. `held` is `keyboard_service`'s own private state, not BDA
/// memory — see the class comment in keyboard.h.
void apply_lock_key(cpu::processor& cpu, bool down, std::uint8_t mask,
                    bool& held) {
  if (!down) {
    held = false;
    return;
  }
  if (held) {
    return;
  }
  held = true;
  set_shift_flag(cpu, mask, !shift_flag(cpu, mask));
}

// --- Translation ---------------------------------------------------------

/// The word AH=00h/01h hand back for `entry`, with `scancode` as AH and
/// the translated ASCII (or 0) as AL — see keyboard.h's `key_kind` for
/// which rule applies to which kind.
[[nodiscard]] std::uint16_t translate(cpu::processor& cpu,
                                      std::uint8_t scancode,
                                      const xt_keyboard::key_entry& entry) {
  const bool shift = shift_flag(cpu, xt_keyboard::left_shift_mask) ||
                     shift_flag(cpu, xt_keyboard::right_shift_mask);
  const bool ctrl = shift_flag(cpu, xt_keyboard::ctrl_mask);
  const bool caps = shift_flag(cpu, xt_keyboard::caps_lock_mask);
  const bool numlock = shift_flag(cpu, xt_keyboard::num_lock_mask);

  char ascii = 0;
  switch (entry.kind) {
    case xt_keyboard::key_kind::letter:
      // The table carries only the lower-case letter (keyboard.h) — the
      // upper-case form is computed here, not looked up, because it is
      // the same computation for all twenty-six of them.
      ascii = ctrl ? static_cast<char>((entry.unshifted - 'a') + 1)
              : (shift != caps)
                  ? static_cast<char>((entry.unshifted - 'a') + 'A')
                  : entry.unshifted;
      break;
    case xt_keyboard::key_kind::ascii:
      ascii = shift ? entry.shifted : entry.unshifted;
      break;
    case xt_keyboard::key_kind::numpad_digit:
      ascii = (numlock != shift) ? entry.numeric : '\0';
      break;
    case xt_keyboard::key_kind::function:
    case xt_keyboard::key_kind::unmapped:
    case xt_keyboard::key_kind::left_shift:
    case xt_keyboard::key_kind::right_shift:
    case xt_keyboard::key_kind::ctrl:
    case xt_keyboard::key_kind::alt:
    case xt_keyboard::key_kind::caps_lock:
    case xt_keyboard::key_kind::num_lock:
    case xt_keyboard::key_kind::scroll_lock:
      ascii = 0;
      break;
  }

  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(scancode) << 8u) |
      static_cast<std::uint8_t>(ascii));
}

// --- INT 16h ---------------------------------------------------------------

/// AH=00h: read (and remove) a keystroke, blocking without lying about
/// virtual time — see keyboard.h's module comment for the whole design.
void keyboard_read(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  if (kb_buffer_empty(cpu)) {
    // Interrupt delivery clears IF (cpu/interrupts.h) — that already
    // happened once when the caller's own INT 16h reached this handler,
    // so the live flag is off right now regardless of what the caller
    // had it set to. A halt that left it off would wait forever: nothing
    // could ever wake it, timer tick included, and "interrupts still
    // delivered while idle" would be a lie. A real BIOS's own AH=00h
    // loop opens with `STI` before its `HLT` for exactly this reason,
    // and this is that STI. It is local to the wait: the stub's IRET,
    // once a keystroke ends it, restores the *caller's* flags word off
    // the stack (service_floor.h), so whatever the caller had IF set to
    // is exactly what it is set to afterwards — this only reaches the
    // live register so the machine is not stuck deaf while parked here.
    cpu.regs().set_flag(cpu::flag::if_, true);
    cpu.halt();
    return;
  }
  cpu.resume();  // undo a halt left over from an earlier empty retry.
  cpu.regs()[cpu::reg16::ax] = kb_buffer_pop(cpu);
}

/// AH=01h: status. ZF set means empty; ZF clear means AX previews the
/// head keystroke, left in the buffer. Never blocks.
void keyboard_status(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const bool empty = kb_buffer_empty(cpu);
  floor.set_caller_flags(
      cpu::flag::with(floor.caller_flags(), cpu::flag::zf, empty));
  if (!empty) {
    cpu.regs()[cpu::reg16::ax] = kb_buffer_peek(cpu);
  }
}

/// AH=02h: shift flags. Cheap and real — AL becomes 40:17, AH untouched.
void keyboard_shift(service_floor& floor) {
  cpu::processor& cpu = floor.box().processor();
  const std::uint16_t ax = cpu.regs()[cpu::reg16::ax];
  cpu.regs()[cpu::reg16::ax] = static_cast<std::uint16_t>(
      (ax & 0xFF00u) | cpu.read_byte(bda::segment, bda::keyboard_shift_flags));
}

/// The one handler INT 16h installs, dispatching on AH the way the floor
/// itself cannot see inside a single vector (machine.h's
/// `stop_unimplemented_service`).
void keyboard_dispatch(service_floor& floor, std::uint8_t /*vector*/) {
  cpu::processor& cpu = floor.box().processor();
  switch (static_cast<std::uint8_t>(cpu.regs()[cpu::reg16::ax] >> 8u)) {
    case 0x00:
      keyboard_read(floor);
      return;
    case 0x01:
      keyboard_status(floor);
      return;
    case 0x02:
      keyboard_shift(floor);
      return;
    default:
      floor.box().stop_unimplemented_service(
          cpu::physical_address(cpu.regs()[cpu::sreg::cs], cpu.regs().ip));
      return;
  }
}

/// The default INT 1Bh body: empty, exactly as a real BIOS's is. The
/// break flag is already set by the time this runs (`keyboard_service`'s
/// own detection, below); this vector exists only as the hook point
/// software may claim, the same shape INT 1Ch is for the timer
/// (service_floor.cpp).
void default_ctrl_break_handler(service_floor& /*floor*/,
                                std::uint8_t /*vector*/) {}

}  // namespace

void keyboard_service::install(service_floor& floor) {
  floor.provide(xt_keyboard::int16_vector, &keyboard_dispatch);
  floor.provide(xt_keyboard::ctrl_break_vector, &default_ctrl_break_handler);
}

void keyboard_service::reset() noexcept {
  caps_lock_held_ = false;
  num_lock_held_ = false;
  scroll_lock_held_ = false;
}

void keyboard_service::save_state(state_sink& out) const {
  out.flag(caps_lock_held_);
  out.flag(num_lock_held_);
  out.flag(scroll_lock_held_);
}

bool keyboard_service::enqueue(machine& mach, std::uint16_t keystroke) {
  return kb_buffer_push(mach.processor(), keystroke);
}

void keyboard_service::drain(machine& mach) {
  if (mach.input().empty()) {
    return;
  }

  key_event ev{};
  while (mach.input().take(ev)) {
    apply(mach, ev.scancode, ev.action == key_action::down);
  }
}

void keyboard_service::apply(machine& mach, std::uint8_t scancode, bool down) {
  cpu::processor& cpu = mach.processor();
  const xt_keyboard::key_entry entry = scancode < xt_keyboard::xt_table.size()
                                           ? xt_keyboard::xt_table[scancode]
                                           : xt_keyboard::key_entry{};

  switch (entry.kind) {
    case xt_keyboard::key_kind::left_shift:
      set_shift_flag(cpu, xt_keyboard::left_shift_mask, down);
      return;
    case xt_keyboard::key_kind::right_shift:
      set_shift_flag(cpu, xt_keyboard::right_shift_mask, down);
      return;
    case xt_keyboard::key_kind::ctrl:
      set_shift_flag(cpu, xt_keyboard::ctrl_mask, down);
      return;
    case xt_keyboard::key_kind::alt:
      set_shift_flag(cpu, xt_keyboard::alt_mask, down);
      return;
    case xt_keyboard::key_kind::caps_lock:
      apply_lock_key(cpu, down, xt_keyboard::caps_lock_mask, caps_lock_held_);
      return;
    case xt_keyboard::key_kind::num_lock:
      apply_lock_key(cpu, down, xt_keyboard::num_lock_mask, num_lock_held_);
      return;
    case xt_keyboard::key_kind::scroll_lock:
      if (down && shift_flag(cpu, xt_keyboard::ctrl_mask)) {
        // Ctrl-Break — see the module comment in keyboard.h.
        cpu.write_byte(bda::segment, bda::keyboard_break_flag,
                       bda::keyboard_break_flag_set);
        cpu.deliver_interrupt(xt_keyboard::ctrl_break_vector);
        return;
      }
      apply_lock_key(cpu, down, xt_keyboard::scroll_lock_mask,
                     scroll_lock_held_);
      return;
    case xt_keyboard::key_kind::unmapped:
      return;
    case xt_keyboard::key_kind::ascii:
    case xt_keyboard::key_kind::letter:
    case xt_keyboard::key_kind::numpad_digit:
    case xt_keyboard::key_kind::function:
      break;
  }

  if (!down) {
    // Break codes for ordinary keys carry no BIOS meaning here — a real
    // BIOS's ISR uses them only for its own typematic bookkeeping, which
    // this machine does not model (platform.h already folds "down" and
    // "repeating" into the same action for the same reason).
    return;
  }

  kb_buffer_push(cpu, translate(cpu, scancode, entry));
}

}  // namespace amberfolio::machine
