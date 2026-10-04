// SPDX-License-Identifier: AGPL-3.0-only
//
// The menu-bar routine's read point, and who called the routine: what
// `bar-keys` (seam_bar_keys.cpp) and `list-arrows` (seam_list_arrows.cpp)
// both need to act on a keystroke before the routine reads it.
//
// **Why this is shared.** Both seams put a point at the same instruction
// (overlay 25, `0x0572`, the call into the program's key-read routine
// inside the menu-bar routine), both rewrite the head of the BIOS ring
// there, and both decide by *who called the routine*. The identification
// is one fact, stated once: a caller is named by the instruction after its
// call, and by the word the program's overlay manager keeps its module's
// load segment in. What each seam does with the answer, and which callers
// it names, is its own (docs/seams.md §10 has both tables).
//
// All of it addresses and offsets. Not one byte of the program is
// reproduced here.

#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"

namespace amberfolio::machine::menu_bar {

// --- The module the menu-bar routine lives in ------------------------------

/// Where the program keeps overlay 25's load segment: the module is the one
/// `list-arrows` documents (docs/seams.md §10).
constexpr std::uint32_t load_segment_at = 0x3C60;

constexpr seam_module module{
    .file = "GAME.OVR",
    .file_offset = 182479,
    .length = 4682,
    .digest =
        "175454bc2f527dd6757c89eaa50a6cdd27a9cf5d3aaa197b33a140f5b09a3901",
    .load_segment_at = load_segment_at};

/// In the menu-bar routine: the call into the key-read routine, reached
/// only when the key-pending routine has said a key is waiting. Offset from
/// the module's start. A handler here runs before the call, so nothing has
/// been read yet.
constexpr std::uint32_t key_read_call = 0x0572;

// --- The routine's frame ---------------------------------------------------

/// Above BP: the caller's return address, offset then segment.
constexpr std::uint16_t frame_return_ip = 2;
constexpr std::uint16_t frame_return_cs = 4;

// --- The data segment ------------------------------------------------------

/// The program's one-byte pushback slot for an extended key's second half.
/// While it is armed the head of the ring is not the key about to be read.
constexpr std::uint16_t data_key_pushback = 0x8501;

// --- The callers -----------------------------------------------------------

/// A caller of the routine, named by the word the program keeps its module's
/// load segment in and the offset of the instruction after its call.
struct caller {
  std::uint32_t load_segment_at;
  std::uint16_t return_offset;
};

/// The words the program's overlay manager keeps the modules' load
/// segments in. Each is the manager's record of the module (its file offset
/// and length, from the overlay table) and the word sixteen bytes into it,
/// found by searching the resident image: one match each. The adventuring
/// loop's and the camp screen's are the words `journal` and `encamp-fix`
/// resolve their own points through; the combat overlay's is
/// `cheat-kill-all`'s.
constexpr std::uint32_t temple_load_segment_at = 0x230;       // overlay 4
constexpr std::uint32_t post_combat_load_segment_at = 0x260;  // overlay 5
constexpr std::uint32_t shop_load_segment_at = 0x290;         // overlay 6
constexpr std::uint32_t script_load_segment_at = 0x2C0;       // overlay 7
constexpr std::uint32_t combat_load_segment_at = 0x360;       // overlay 8
constexpr std::uint32_t aim_load_segment_at = 0x690;          // overlay 13
constexpr std::uint32_t adventure_load_segment_at = 0x730;    // overlay 14
constexpr std::uint32_t camp_load_segment_at = 0x760;         // overlay 15
constexpr std::uint32_t roster_load_segment_at = 0x790;       // overlay 16
constexpr std::uint32_t slots_load_segment_at = 0x7D0;        // overlay 17
constexpr std::uint32_t view_load_segment_at = 0x860;         // overlay 19
constexpr std::uint32_t rest_load_segment_at = 0x8D0;         // overlay 20
constexpr std::uint32_t appraise_load_segment_at = 0x900;     // overlay 21

/// The segment the program says `at` is loaded at now; zero while it is
/// not loaded.
[[nodiscard]] inline std::uint16_t loaded_at(cpu::processor& cpu,
                                             const seam_context& ctx,
                                             std::uint32_t at) {
  return cpu.read_word(static_cast<std::uint16_t>(ctx.image_base() >> 4U),
                       static_cast<std::uint16_t>(at));
}

/// Whether this call of the routine, whose frame is at SS:BP, came from a
/// caller in `table`: the return offset is the caller's and the return
/// segment is the one the manager says that caller's module is at now.
[[nodiscard]] inline bool called_from(cpu::processor& cpu,
                                      const seam_context& ctx,
                                      std::span<const caller> table) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t ip =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_ip));
  const std::uint16_t cs =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_cs));

  for (const caller& c : table) {
    if (ip != c.return_offset) {
      continue;
    }
    const std::uint16_t segment = loaded_at(cpu, ctx, c.load_segment_at);
    if (segment != 0 && segment == cs) {
      return true;
    }
  }
  return false;
}

// --- The keystroke about to be read ----------------------------------------

/// The keystroke at the head of the BIOS ring as the BIOS keeps it (scan
/// code high, character low), and where in the ring it is.
struct pending_key {
  std::uint16_t at;
  std::uint16_t key;
};

/// The keystroke the program is about to read, if it is one of `wanted` and
/// the handler may treat the head of the ring as that keystroke. Not when
/// the ring is empty (the poll was answered from the pushback slot), not
/// while the journal reader is open (it takes every key at the poll, so one
/// that got past is the reader's), and not while the pushback slot is armed
/// (the head of the ring is then not the key about to be read). A key that
/// is not one of `wanted` costs two reads and nothing else.
[[nodiscard]] inline std::optional<pending_key> key_about_to_be_read(
    machine& box, std::span<const std::uint16_t> wanted) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  if (head == cpu.read_word(bda::segment, bda::keyboard_buffer_tail)) {
    return std::nullopt;
  }
  const std::uint16_t key = cpu.read_word(bda::segment, head);
  bool is_wanted = false;
  for (const std::uint16_t w : wanted) {
    is_wanted = is_wanted || key == w;
  }
  if (!is_wanted) {
    return std::nullopt;
  }
  if (box.journal().reader_open()) {
    return std::nullopt;
  }
  if (cpu.read_byte(cpu.regs()[cpu::sreg::ds], data_key_pushback) != 0) {
    return std::nullopt;
  }
  return pending_key{.at = head, .key = key};
}

}  // namespace amberfolio::machine::menu_bar
