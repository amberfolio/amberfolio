// SPDX-License-Identifier: AGPL-3.0-only
//
// BIOS keyboard services: INT 16h, the BDA keystroke buffer at 0040:001E,
// and the Ctrl-Break path. PLAN.md §3's "BIOS keyboard services (poll /
// blocking read) and the Ctrl-Break path" — M2-D8, #53.
//
// service_floor.h lays out where the buffer, its pointers and the
// shift-flag byte live in the BDA (they are facts about the machine's
// memory map); this file is what keeps them true. Host key events arrive
// as raw XT make/break scan codes through `machine::post_key()`
// (platform.h says why — programs read 40:17 directly, so the shift
// state has to live in real memory and cannot also live in the host).
// `keyboard_service::drain()` is what turns a scan code into a keystroke:
// it tracks shift/ctrl/alt/lock state, translates through the table
// below, and enqueues into the real buffer, natively, exactly as a
// program would find it if IRQ1 had done the work.
//
//
// No IRQ1, and what stands in for it
// -----------------------------------
//
// M2-D1 (#46) settled that this milestone has no 8259, no INT 09h, and
// no port 60h: host key events enter at the BIOS-buffer level, not the
// hardware level. `keyboard_service::drain()` is therefore doing the job
// INT 09h's ISR would have done — translating a scan code and filling
// the buffer — except it is called directly out of `machine::step()`
// once per scheduling step, unconditionally, rather than being an
// interrupt a controller raises. A program that overwrites vector 09h
// gets nothing from us either way and earns a log line (machine.cpp),
// which is how M3 will tell us whether the game needs the real hardware
// path after all.
//
// Calling it every step is what makes "a program that reads 40:1E
// directly" (the exit criterion, and one of the unit tests) work without
// any INT 16h call at all: `machine::step()` drains input and settles
// the buffer *before* it lets the processor run, so the buffer a program
// finds by reading raw memory is never stale by more than the step that
// is about to happen. `input_queue::empty()` makes the common case one
// branch, the same cost model the service floor's own CS compare uses.
//
//
// AH=00h on an empty buffer: idle, do not lie about time
// --------------------------------------------------------
//
// This is the one subtle part of the issue. A real keyboard wait loop is
// `STI` then `HLT` then re-check — the BIOS's own AH=00h does the
// equivalent by looping internally until a keystroke shows up. Doing
// that literally here — spin inside the handler until the buffer is
// non-empty — is not available to us: nothing *else* makes a keystroke
// appear while a native handler is running, because the whole machine is
// one function call deep in it. Spinning the host thread would either
// hang (no host key event is coming while we never return control) or,
// worse, silently pump virtual time without the rest of the machine
// (deadlines, other interrupts) ever getting a turn — lying about time
// exactly as PLAN.md's "log, don't fake" rule forbids.
//
// The design of record instead makes the *machine* idle, HLT-style,
// between scheduling steps, so every other part of the machine keeps
// running normally while a keystroke is awaited:
//
//   * `keyboard_read()`, finding the buffer empty, sets IF on the live
//     register file and calls `processor::halt()` — nothing else
//     changes, not AX, not CS:IP. The IF set is load-bearing and easy to
//     miss: delivering the caller's own INT 16h already cleared it
//     (cpu/interrupts.h, every delivery does), so without this a halt
//     here would wait on interrupts it has itself switched off — deaf to
//     the very timer tick that is supposed to keep proving virtual time
//     has not stopped. A real BIOS's AH=00h loop opens with `STI` before
//     its `HLT` for the same reason; this is that `STI`, and it is local
//     to the wait — the stub's IRET restores the *caller's* flags word
//     off the stack when the read finally completes, so whatever the
//     caller had IF set to is exactly what it is set to afterwards.
//     CS:IP is already sitting exactly on the INT 16h stub (that is
//     where a service handler always runs, service_floor.h), so there is
//     nothing to "back up": the point is that it *stays* there.
//   * Back in `machine::step()`, `cpu_.step()` sees the halt before it
//     would fetch the stub's IRET and reports `halted` without touching
//     the bus — the IRET never runs, so control never returns to the
//     caller with a bogus answer.
//   * The clock still advances by `step_cost()` every step regardless
//     (machine.h says why: a halted machine is waiting for an interrupt,
//     and only the clock moving can bring one), and `service_interrupt()`
//     is checked before the halt on every one of those steps, so a timer
//     tick or a Ctrl-Break still fires on schedule.
//   * Because CS:IP never left the stub, `machine::step()`'s own
//     `CS == stub_segment` check fires again on the very next step and
//     dispatches this same handler again — which is what "re-enter the
//     stub" means here: not a jump backwards, but never having left. On
//     that re-entry the handler finds whatever `keyboard_service::drain`
//     has enqueued since (drain runs before the dispatch check, every
//     step, so the buffer is never stale), and either halts again or
//     answers and calls `processor::resume()` to undo the halt.
//
// The net effect is a caller that called AH=00h with an empty buffer
// gets its answer on the very step a keystroke becomes available, having
// spent the wait as `halted` — indistinguishable, from outside the
// machine, from a real HLT loop, and burning no more host CPU per
// virtual tick than any other step does.
//
//
// Ctrl-Break: the original Break key, not the 101-key Pause sequence
// ---------------------------------------------------------------------
//
// The IBM PC/XT 83-key keyboard has no Pause key and no E1 prefix — that
// arrived with the 101-key AT keyboard. Its Break function is the second
// legend on the Scroll Lock key: Ctrl held down while Scroll Lock goes
// down is Ctrl-Break, and the keyboard sends nothing the host has to
// treat specially for it — it is an ordinary make code (0x46) that
// happens to arrive while our own Ctrl flag is set. `drain()` detects
// exactly that combination, sets the BDA break flag at 40:71 (a fact,
// not a game artifact — CONTRIBUTING.md), and calls
// `processor::deliver_interrupt` on INT 1Bh directly, the same way the
// timer's default handler calls it on the user tick vector
// (service_floor.cpp): a native handler doing what BIOS assembly would
// do with its own `INT 1Bh` instruction, unconditionally and not gated
// on IF, because a software interrupt never is. INT 1Bh is hookable —
// this file provides a default empty handler at it (below) purely so an
// unhooked Ctrl-Break does not reach `stop_reason::unimplemented_service`
// the way a truly absent vector would (service_floor.h). Ctrl-C typed
// normally is not this path at all: it is the 'c' key with Ctrl held,
// which `translate()` turns into the ordinary control code 0x03 and
// enqueues like any other keystroke — DOS's own Ctrl-C/Ctrl-Break
// checking on top of that is M2-D7's (#52), not ours.
//
//
// What the table does not model
// ------------------------------
//
// The XT scan code set below is complete for all 83 keys (0x01-0x53),
// but three real BIOS behaviours are deliberately left out because
// nothing in this milestone's scope exercises them and inventing an
// unverified answer would be worse than an honest gap:
//
//   * Ctrl combined with punctuation (Ctrl-[, Ctrl-\, Ctrl-6, ...) has
//     well-known control-code answers on real hardware; only Ctrl+letter
//     (0x01-0x1A, Ctrl-C included) is modelled. A Ctrl+punctuation combo
//     reads AL=0, AH=<its ordinary scan code>, same as any other key we
//     do not give a control meaning to.
//   * Shifted/Ctrl'd/Alt'd F-keys have distinct BIOS "extended" AH values
//     on a real machine; here every F-key always answers AL=0,
//     AH=<its own raw scan code>, whatever is held.
//   * The numeric keypad's navigation function (Home/Up/PgUp/...) reads
//     AL=0, AH=<its own raw scan code> rather than a separate extended
//     code — true for the base 83-key keyboard, which has only one scan
//     code per physical key to report in the first place.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace amberfolio::machine {

class machine;
class service_floor;

/// Facts about the IBM PC/XT 83-key keyboard: XT scan code set 1, the US
/// QWERTY legends printed on it, and the shift-flag bit layout at 40:17.
/// A fact table, not game content — CONTRIBUTING.md's clean-content rule
/// is explicit that hardware facts (addresses, encodings, layouts) are
/// fine; nothing below came from any game.
namespace xt_keyboard {

// --- The shift-flag byte, 40:17 ----------------------------------------
//
// Bit positions a real BIOS defines, quoted here as masks because that
// is the form every caller wants.

inline constexpr std::uint8_t right_shift_mask = 0x01;
inline constexpr std::uint8_t left_shift_mask = 0x02;
inline constexpr std::uint8_t ctrl_mask = 0x04;
inline constexpr std::uint8_t alt_mask = 0x08;
inline constexpr std::uint8_t scroll_lock_mask = 0x10;
inline constexpr std::uint8_t num_lock_mask = 0x20;
inline constexpr std::uint8_t caps_lock_mask = 0x40;

/// The Scroll Lock/Break key's scan code — see the module comment on why
/// Ctrl held plus this key is Ctrl-Break on this keyboard.
inline constexpr std::uint8_t scroll_lock_scancode = 0x46;

/// The vector INT 16h answers, and the one Ctrl-Break raises. Named here
/// rather than beside service_floor.h's `timer_vector` because this file,
/// not that one, is what provides handlers for them (service_floor.h's
/// own comment says why the split is there).
inline constexpr std::uint8_t int16_vector = 0x16;
inline constexpr std::uint8_t ctrl_break_vector = 0x1B;

/// What a scan code means, once the modifier keys are set apart from
/// everything else. `letter` is its own kind rather than folded into
/// `ascii` because a letter's case depends on shift *and* Caps Lock
/// together (they XOR) and its Ctrl form is computed, not tabulated;
/// `numpad_digit` is its own kind for the same reason — Num Lock and
/// shift XOR to decide digit-vs-navigation, and the two symbol keys on
/// the pad (+ and -) are ordinary `ascii` entries instead, because they
/// are not affected by Num Lock at all.
enum class key_kind : std::uint8_t {
  /// A scan code with nothing behind it. Not a stop and not logged —
  /// this is a key that sends nothing meaningful, which is not the same
  /// thing as a program asking the BIOS for a service we do not have.
  unmapped,
  /// An ordinary printable key: `unshifted` or `shifted` depending on
  /// the shift keys alone.
  ascii,
  /// A-Z: case is `shift XOR caps_lock`; Ctrl held answers the control
  /// code for the letter (0x01-0x1A) regardless of shift or caps.
  letter,
  /// The numeric keypad's digit-bearing keys: `numeric` when
  /// `num_lock XOR shift`, AL=0 (a navigation function, not modelled —
  /// see the module comment) otherwise.
  numpad_digit,
  /// F1-F10. AL=0, AH=the scan code, always — see the module comment.
  function,
  left_shift,
  right_shift,
  ctrl,
  alt,
  caps_lock,
  num_lock,
  scroll_lock,
};

struct key_entry {
  key_kind kind{key_kind::unmapped};
  /// `ascii` and `letter` kinds only: the character with no shift key
  /// held (for `letter`, always lower case — the table is not what
  /// decides its case).
  char unshifted{};
  /// `ascii` kind only: the character with a shift key held. `letter`
  /// computes its own upper-case form from `unshifted`.
  char shifted{};
  /// `numpad_digit` kind only: the digit character.
  char numeric{};
};

/// One entry per XT make code, 0x00-0x53 — the 83-key keyboard is
/// 0x01-0x53 exactly, and slot 0 is never a make code so it is left as
/// `unmapped`.
inline constexpr std::size_t table_size = 0x54;

namespace detail {

[[nodiscard]] constexpr std::array<key_entry, table_size> build_xt_table() {
  std::array<key_entry, table_size> t{};

  // Esc, then the number row: unshifted digit, shifted the symbol printed
  // above it on a US QWERTY keycap.
  t[0x01] = {.kind = key_kind::ascii, .unshifted = '\x1B', .shifted = '\x1B'};
  t[0x02] = {.kind = key_kind::ascii, .unshifted = '1', .shifted = '!'};
  t[0x03] = {.kind = key_kind::ascii, .unshifted = '2', .shifted = '@'};
  t[0x04] = {.kind = key_kind::ascii, .unshifted = '3', .shifted = '#'};
  t[0x05] = {.kind = key_kind::ascii, .unshifted = '4', .shifted = '$'};
  t[0x06] = {.kind = key_kind::ascii, .unshifted = '5', .shifted = '%'};
  t[0x07] = {.kind = key_kind::ascii, .unshifted = '6', .shifted = '^'};
  t[0x08] = {.kind = key_kind::ascii, .unshifted = '7', .shifted = '&'};
  t[0x09] = {.kind = key_kind::ascii, .unshifted = '8', .shifted = '*'};
  t[0x0A] = {.kind = key_kind::ascii, .unshifted = '9', .shifted = '('};
  t[0x0B] = {.kind = key_kind::ascii, .unshifted = '0', .shifted = ')'};
  t[0x0C] = {.kind = key_kind::ascii, .unshifted = '-', .shifted = '_'};
  t[0x0D] = {.kind = key_kind::ascii, .unshifted = '=', .shifted = '+'};
  t[0x0E] = {.kind = key_kind::ascii, .unshifted = '\b', .shifted = '\b'};
  t[0x0F] = {.kind = key_kind::ascii, .unshifted = '\t', .shifted = '\t'};

  // QWERTYUIOP, and the bracket keys beside P.
  t[0x10] = {.kind = key_kind::letter, .unshifted = 'q'};
  t[0x11] = {.kind = key_kind::letter, .unshifted = 'w'};
  t[0x12] = {.kind = key_kind::letter, .unshifted = 'e'};
  t[0x13] = {.kind = key_kind::letter, .unshifted = 'r'};
  t[0x14] = {.kind = key_kind::letter, .unshifted = 't'};
  t[0x15] = {.kind = key_kind::letter, .unshifted = 'y'};
  t[0x16] = {.kind = key_kind::letter, .unshifted = 'u'};
  t[0x17] = {.kind = key_kind::letter, .unshifted = 'i'};
  t[0x18] = {.kind = key_kind::letter, .unshifted = 'o'};
  t[0x19] = {.kind = key_kind::letter, .unshifted = 'p'};
  t[0x1A] = {.kind = key_kind::ascii, .unshifted = '[', .shifted = '{'};
  t[0x1B] = {.kind = key_kind::ascii, .unshifted = ']', .shifted = '}'};
  t[0x1C] = {.kind = key_kind::ascii, .unshifted = '\r', .shifted = '\r'};
  t[0x1D] = {.kind = key_kind::ctrl};

  // ASDFGHJKL, semicolon, quote, backtick.
  t[0x1E] = {.kind = key_kind::letter, .unshifted = 'a'};
  t[0x1F] = {.kind = key_kind::letter, .unshifted = 's'};
  t[0x20] = {.kind = key_kind::letter, .unshifted = 'd'};
  t[0x21] = {.kind = key_kind::letter, .unshifted = 'f'};
  t[0x22] = {.kind = key_kind::letter, .unshifted = 'g'};
  t[0x23] = {.kind = key_kind::letter, .unshifted = 'h'};
  t[0x24] = {.kind = key_kind::letter, .unshifted = 'j'};
  t[0x25] = {.kind = key_kind::letter, .unshifted = 'k'};
  t[0x26] = {.kind = key_kind::letter, .unshifted = 'l'};
  t[0x27] = {.kind = key_kind::ascii, .unshifted = ';', .shifted = ':'};
  t[0x28] = {.kind = key_kind::ascii, .unshifted = '\'', .shifted = '"'};
  t[0x29] = {.kind = key_kind::ascii, .unshifted = '`', .shifted = '~'};
  t[0x2A] = {.kind = key_kind::left_shift};
  t[0x2B] = {.kind = key_kind::ascii, .unshifted = '\\', .shifted = '|'};

  // ZXCVBNM, comma, period, slash.
  t[0x2C] = {.kind = key_kind::letter, .unshifted = 'z'};
  t[0x2D] = {.kind = key_kind::letter, .unshifted = 'x'};
  t[0x2E] = {.kind = key_kind::letter, .unshifted = 'c'};
  t[0x2F] = {.kind = key_kind::letter, .unshifted = 'v'};
  t[0x30] = {.kind = key_kind::letter, .unshifted = 'b'};
  t[0x31] = {.kind = key_kind::letter, .unshifted = 'n'};
  t[0x32] = {.kind = key_kind::letter, .unshifted = 'm'};
  t[0x33] = {.kind = key_kind::ascii, .unshifted = ',', .shifted = '<'};
  t[0x34] = {.kind = key_kind::ascii, .unshifted = '.', .shifted = '>'};
  t[0x35] = {.kind = key_kind::ascii, .unshifted = '/', .shifted = '?'};
  t[0x36] = {.kind = key_kind::right_shift};
  // Numpad *, also PrtSc on the 83-key board (PrtSc's BIOS-call behaviour
  // is not modelled — nothing here treats it differently from a literal
  // '*' keystroke).
  t[0x37] = {.kind = key_kind::ascii, .unshifted = '*', .shifted = '*'};
  t[0x38] = {.kind = key_kind::alt};
  t[0x39] = {.kind = key_kind::ascii, .unshifted = ' ', .shifted = ' '};
  t[0x3A] = {.kind = key_kind::caps_lock};

  // F1-F10.
  for (std::uint8_t i = 0; i < 10; ++i) {
    t[static_cast<std::size_t>(0x3B + i)] = {.kind = key_kind::function};
  }

  t[0x45] = {.kind = key_kind::num_lock};
  t[0x46] = {.kind = key_kind::scroll_lock};

  // The numeric keypad. Minus and plus are dedicated keys, unaffected by
  // Num Lock; the rest are digit-bearing and toggle between the digit and
  // its navigation meaning (module comment).
  t[0x47] = {.kind = key_kind::numpad_digit, .numeric = '7'};  // Home
  t[0x48] = {.kind = key_kind::numpad_digit, .numeric = '8'};  // Up
  t[0x49] = {.kind = key_kind::numpad_digit, .numeric = '9'};  // PgUp
  t[0x4A] = {.kind = key_kind::ascii, .unshifted = '-', .shifted = '-'};
  t[0x4B] = {.kind = key_kind::numpad_digit, .numeric = '4'};  // Left
  t[0x4C] = {.kind = key_kind::numpad_digit, .numeric = '5'};  // centre
  t[0x4D] = {.kind = key_kind::numpad_digit, .numeric = '6'};  // Right
  t[0x4E] = {.kind = key_kind::ascii, .unshifted = '+', .shifted = '+'};
  t[0x4F] = {.kind = key_kind::numpad_digit, .numeric = '1'};  // End
  t[0x50] = {.kind = key_kind::numpad_digit, .numeric = '2'};  // Down
  t[0x51] = {.kind = key_kind::numpad_digit, .numeric = '3'};  // PgDn
  t[0x52] = {.kind = key_kind::numpad_digit, .numeric = '0'};  // Ins
  t[0x53] = {.kind = key_kind::numpad_digit, .numeric = '.'};  // Del

  return t;
}

}  // namespace detail

inline constexpr std::array<key_entry, table_size> xt_table =
    detail::build_xt_table();

}  // namespace xt_keyboard

/// The BIOS keyboard service: installs INT 16h and the default Ctrl-Break
/// hook, and turns host key events into BDA state every scheduling step.
///
/// Owned by `machine`, one per machine — see the module comment for the
/// mechanism. What it keeps beyond the BDA is three booleans, and they
/// exist only to tell a fresh Caps/Num/Scroll Lock press from the host
/// re-sending `key_action::down` for an already-held key (platform.h says
/// a host cannot tell a make from a repeat and neither can this): without
/// them, a key that auto-repeats while held would toggle its lock state
/// on every repeat instead of once per press.
class keyboard_service {
 public:
  /// Provide INT 16h and the default (empty) INT 1Bh hook point. Called
  /// once, from `machine`'s constructor, the same way `service_floor`
  /// installs its own default timer handler.
  void install(service_floor& floor);

  /// Drain every event `mach.input()` is holding into BDA state: shift
  /// flags, the keystroke buffer, Ctrl-Break. Called from
  /// `machine::step()` on every scheduling step — see the module comment
  /// for why every step and not only when INT 16h is called.
  void drain(machine& mach);

  /// The RESET line. Only the auto-repeat bookkeeping above: the BDA
  /// itself is rewritten by `service_floor::reset()`, like the rest of
  /// the BDA, because it is memory and RESET does not selectively forget
  /// parts of memory.
  void reset() noexcept;

  /// Put one translated keystroke — scan code in the high byte, character
  /// (or 0) in the low — straight into the BDA buffer, as `drain()` would
  /// have after translating a host event. False, and nothing written, if
  /// the buffer is full, exactly as a typed key is dropped.
  ///
  /// The seam engine's synthetic-input funnel (seam.h, PLAN.md §5 item
  /// 3) and nothing else's: a host posts scan codes through
  /// `machine::post_key()`, which is the recordable stream, and a seam
  /// enters here precisely because its keystrokes are not part of that
  /// stream. Reached through `machine::inject_keystroke()`.
  bool enqueue(machine& mach, std::uint16_t keystroke);

 private:
  /// Apply one event: update modifier/lock state, detect Ctrl-Break, or
  /// translate and enqueue an ordinary keystroke.
  void apply(machine& mach, std::uint8_t scancode, bool down);

  bool caps_lock_held_{false};
  bool num_lock_held_{false};
  bool scroll_lock_held_{false};
};

}  // namespace amberfolio::machine
