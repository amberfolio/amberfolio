// SPDX-License-Identifier: AGPL-3.0-only
//
// The roster fix: a character the roster names and the save directory
// does not hold is left off the list (Add) offers.
//
// **A built-in fix, not a listed seam** (`built_in_fixes()`, seam.h): on
// whenever the program is loaded, and in no panel, config file, flag or
// recording. There is no player for whom a name that hangs the game when
// picked is worth keeping, so there is no choice to offer.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// **The roster is two things that the program itself lets drift apart.**
// `CHARLIST.TXT` in the save directory is a list of names, one to a line;
// each name's record is a file of its own beside it, `<STEM>.CHA`, where
// the stem is the name with ten characters taken out, cut to eight and
// upper-cased. Loading a saved game **unlinks** the `.CHA` of every party
// member it restores, and the list is pruned only the next time the
// program writes it, and then only of names that match a party member's
// in full. Leave between the two (load a slot, play, quit, load another)
// and the list names a character whose file is gone. Two names that cut to
// one stem share one file, so unlinking it for one strands the other.
//
// **(A)dd then hangs on that name.** The party-setup loop's Add command
// loads the list, lets the player pick, and opens the pick's file through
// the program's own open-or-ask routine, which asks for the save disk and
// waits for a key **for as long as the file is not there**. On a machine
// with a floppy door that was a request; on this one the save directory
// is the directory, nothing a player can do makes the file appear, and
// the only way out is to stop the machine. Met on the web build, where a
// browser's store is the save directory.
//
// **The list is built by one routine**, in overlay 17, and both of its
// callers go through it: the Add command, and the character generator,
// which appends the character it has just made and writes the list back.
// It builds a linked list of 46-byte nodes on the program's heap — a
// Pascal string of up to forty characters at `+0x00`, a far pointer to the
// next at `+0x2A` — through a far pointer to the head its caller passes
// (`[BP+6]`), and marks a name already in the party with a leading `* `,
// which the Add command refuses and the list writer drops. `list_exit` is
// its one exit, shared by the path with no list file and the path that
// read one, with the frame still up.
//
//
// What the seam does
// ------------------
//
// **At that exit it asks, of every unmarked name, the question the
// program's open is about to ask**: the save directory (the program's own
// Pascal string, `save_path`), the stem cut the program's way (with the
// ten characters read out of the program's own table, `strip_table`, so
// none of them is written down here), `.CHA`, canonicalized against the
// current directory exactly as INT 21h would canonicalize it, and looked
// up in the machine's filesystem. A name whose file is there is left
// alone. A name whose file is not is **unlinked from the list and its
// node handed back to the program's heap** through the program's own
// FreeMem, one per arrival: the batch ends by offering the point again
// (docs/seams.md §3), and the next arrival finds the next one.
//
// What follows is the program's. The Add command shows the list without
// those names, and writes it back without them when the player leaves —
// the list writer writes what is in the list — so `CHARLIST.TXT` comes to
// say what the directory holds. A list left with nobody in it is the
// list the program finds when there is no file, and Add returns at once,
// as it does then.
//
// **What it refuses.** Every name the program has marked: it is in the
// party, its file was unlinked by the load that put it there, and the
// program already handles it. A frame or a node it cannot follow inside
// conventional memory, a name longer than a node holds, a list longer
// than any the program could build, a save directory that does not
// resolve to a directory, or no filesystem at all: declined, untouched.
// The last two matter most. The seam's whole evidence is "the directory
// does not hold this file", and that is only evidence when there is a
// directory to ask.
//
// **Rejected: refusing the pick instead.** The proven design left the
// list alone and refused the pick with a line on the message row, on the
// argument that "not there right now" might be a save store still filling
// in. Here it cannot be: a host puts every file on the machine before the
// program's first instruction, and the program's own open is answered from
// the same filesystem this seam asks. And the program's line-and-a-key is
// one routine that waits for a person, which a batch of calls cannot hold
// (its step budget bounds never, not a player reading).
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// **Over a roster with every file in place it moves nothing**: it reads
// the frame, the list, the data segment and the filesystem, and writes
// none of them. That is the whole of why a fix may be on without being
// asked: every recording in `tests/sessions/` was made without it and
// verifies with it, `party` among them, which reaches it twice. Over a
// stranded name it changes the list and, when the player leaves Add, the
// file — the change it exists for, held by `roster-add`.
//
//
// What it is not yet
// ------------------
//
// Nothing outstanding.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/vfs.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition's program image (edition.h). The store copies boot
/// the same one, and their GAME.OVR differs from it only in overlay 2.
constexpr std::array<std::string_view, 1> roster_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The module the list routine lives in ----------------------------------

/// Overlay 17: file offset 122854 (`0x1DFE6`), 9189 bytes (`0x23E5`), the
/// load-segment word at image `0x07D0` (the manager's record is at
/// `0x07C0`, the one match of the search `seam_cheats.cpp` documents).
constexpr seam_module list_module{
    .file = "GAME.OVR",
    .file_offset = 122854,
    .length = 9189,
    .digest =
        "30da2d2a28149942431395bf59511b55a9eab4d8f728bff625c5a22d81638882",
    .load_segment_at = 0x7D0};

/// The list routine's one exit, `mov sp, bp`: the frame is still up.
constexpr std::uint32_t list_exit = 0x0191;

/// Above BP: the far pointer to the caller's head pointer.
constexpr std::uint16_t frame_head = 6;

// --- The list's nodes ------------------------------------------------------

constexpr std::uint16_t node_bytes = 0x2E;
constexpr std::uint16_t node_next = 0x2A;
constexpr std::uint8_t node_longest_name = 0x28;

/// The mark the routine puts in front of a name already in the party.
constexpr std::uint8_t in_party_mark = '*';

/// More names than the program's heap could hold nodes for; a walk that
/// gets this far is going round in a circle, not down a list.
constexpr unsigned longest_list = 4096;

// --- The data segment ------------------------------------------------------

/// The save directory, a Pascal string.
constexpr std::uint16_t save_path = 0x537A;
/// Longer than any directory the configuration line it is read from can
/// name; a length past it is not the string these facts describe.
constexpr std::uint8_t save_path_longest = 0x50;

/// The characters a name loses on its way to a stem: ten bytes, one each.
constexpr std::uint16_t strip_table = 0x0D1A;
constexpr std::size_t strip_count = 10;

/// What is left of a name: eight characters.
constexpr std::size_t stem_longest = 8;

// --- The heap --------------------------------------------------------------

/// The program's FreeMem: paragraph and offset from the image segment, and
/// pushed first to last the pointer's segment, its offset, and the size.
/// It cleans its own six bytes (`retf 6`).
constexpr std::uint16_t free_memory_paragraph = 0x0AF8;
constexpr std::uint16_t free_memory_offset = 0x0254;

struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;
};

[[nodiscard]] std::uint16_t at(std::uint16_t offset,
                               std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(offset + by);
}

[[nodiscard]] far_pointer far_at(cpu::processor& cpu, std::uint16_t segment,
                                 std::uint16_t offset) {
  return {.offset = cpu.read_word(segment, offset),
          .segment = cpu.read_word(segment, at(offset, 2))};
}

[[nodiscard]] bool is_null(const far_pointer& pointer) noexcept {
  return pointer.segment == 0 && pointer.offset == 0;
}

/// Whether a far pointer names `length` bytes of conventional memory. A
/// read above it is a read of the video window, which loads the latches.
[[nodiscard]] bool followable(const far_pointer& pointer,
                              std::uint32_t length) noexcept {
  if (pointer.segment == 0 || length == 0) {
    return false;
  }
  if (static_cast<std::uint32_t>(pointer.offset) + length > 0x10000U) {
    return false;
  }
  return cpu::physical_address(pointer.segment, pointer.offset) + length <=
         conventional_ram_size;
}

/// A path as the program would hand it to INT 21h: the save directory, a
/// stem, an extension. Long enough for the longest directory and the rest.
struct path_text {
  std::array<char, save_path_longest + stem_longest + 4> chars{};
  std::size_t size{};

  void push(char c) noexcept {
    if (size < chars.size()) {
      chars[size++] = c;
    }
  }

  [[nodiscard]] std::span<const char> view() const noexcept {
    return {chars.data(), size};
  }
};

/// The save directory, out of the program's data segment.
[[nodiscard]] bool read_save_path(cpu::processor& cpu, std::uint16_t ds,
                                  path_text& out) {
  const std::uint8_t length = cpu.read_byte(ds, save_path);
  if (length > save_path_longest) {
    return false;
  }
  for (std::uint8_t i = 0; i < length; ++i) {
    out.push(static_cast<char>(
        cpu.read_byte(ds, at(save_path, static_cast<std::uint16_t>(1U + i)))));
  }
  return true;
}

/// Whether `text` resolves to a directory this machine holds.
[[nodiscard]] bool names_a_directory(const machine& box,
                                     std::span<const char> text) {
  const vfs_result<dos_path> where =
      canonicalize(box.dos().current_directory(), text);
  if (!where.ok()) {
    return false;
  }
  if (where.value.is_root()) {
    return true;
  }
  const vfs_result<file_stat> stat = box.vfs()->stat(where.value);
  return stat.ok() && stat.value.is_directory;
}

/// The question the program's open is about to ask of the name at `node`:
/// is its file in the save directory.
[[nodiscard]] bool has_a_file(const machine& box, cpu::processor& cpu,
                              const far_pointer& node,
                              const path_text& directory,
                              std::span<const std::uint8_t> strip) {
  path_text path = directory;
  const std::uint8_t length = cpu.read_byte(node.segment, node.offset);
  std::size_t kept = 0;
  for (std::uint8_t i = 0; i < length && kept < stem_longest; ++i) {
    const std::uint8_t c = cpu.read_byte(
        node.segment, at(node.offset, static_cast<std::uint16_t>(1U + i)));
    bool stripped = false;
    for (const std::uint8_t s : strip) {
      stripped = stripped || c == s;
    }
    if (stripped) {
      continue;
    }
    path.push(static_cast<char>(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c));
    ++kept;
  }
  for (const char c : std::string_view{".CHA"}) {
    path.push(c);
  }
  // A stem DOS cannot spell is a file the program's open cannot find on
  // this machine either: the same answer, by the same canonicalization.
  const vfs_result<dos_path> where =
      canonicalize(box.dos().current_directory(), path.view());
  return where.ok() && box.vfs()->exists(where.value);
}

/// The list routine's exit: unlink the first name whose file is not
/// there, and hand its node back to the heap.
void leave_out_the_stranded(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];

  if (box.vfs() == nullptr) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const far_pointer head_at = far_at(cpu, ss, at(bp, frame_head));
  if (!followable(head_at, 4)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  path_text directory;
  if (!read_save_path(cpu, ds, directory) ||
      !names_a_directory(box, directory.view())) {
    // No directory to ask, so "not in it" is no evidence of anything.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  std::array<std::uint8_t, strip_count> strip{};
  for (std::size_t i = 0; i < strip_count; ++i) {
    strip[i] =
        cpu.read_byte(ds, at(strip_table, static_cast<std::uint16_t>(i)));
  }

  // Walk the whole list before touching any of it: a node the walk cannot
  // follow anywhere in it means the list is not what these facts describe,
  // and nothing in it is to be trusted, the names before it included.
  // `link` is where the pointer to the node being looked at lives: the
  // caller's head, then each node's next.
  far_pointer link = head_at;
  far_pointer node = far_at(cpu, link.segment, link.offset);
  bool found = false;
  far_pointer stranded_link{};
  far_pointer stranded{};
  far_pointer stranded_next{};
  for (unsigned walked = 0; !is_null(node); ++walked) {
    if (walked == longest_list || !followable(node, node_bytes)) {
      ctx.decline(seam_reason::point_not_recognized);
      return;
    }
    const std::uint8_t length = cpu.read_byte(node.segment, node.offset);
    if (length > node_longest_name) {
      ctx.decline(seam_reason::point_not_recognized);
      return;
    }
    const far_pointer next =
        far_at(cpu, node.segment, at(node.offset, node_next));
    const bool marked =
        length > 0 &&
        cpu.read_byte(node.segment, at(node.offset, 1)) == in_party_mark;
    if (!found && !marked && !has_a_file(box, cpu, node, directory, strip)) {
      found = true;
      stranded_link = link;
      stranded = node;
      stranded_next = next;
    }
    link = {.offset = at(node.offset, node_next), .segment = node.segment};
    node = next;
  }
  if (!found) {
    return;
  }

  // Queue the free first: a batch that will not take it leaves the list as
  // the program built it. The batch ends by offering this point again, and
  // that arrival finds the next one.
  const std::array<std::uint16_t, 3> words{stranded.segment, stranded.offset,
                                           node_bytes};
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  if (!ctx.call_program(at(image, free_memory_paragraph), free_memory_offset,
                        words)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  cpu.write_word(stranded_link.segment, stranded_link.offset,
                 stranded_next.offset);
  cpu.write_word(stranded_link.segment, at(stranded_link.offset, 2),
                 stranded_next.segment);
}

constexpr std::array<seam_point, 1> roster_fix_points{
    {{.module = list_module,
      .offset = list_exit,
      .run = &leave_out_the_stranded}}};

constexpr seam_definition roster_fix_definition{
    .id = "roster-fix",
    .about =
        "leave a character whose file is gone off the Add list, "
        "instead of asking for a save disk for ever",
    .fingerprints = roster_binaries,
    .points = roster_fix_points,
    // Acts where the program builds the list, whenever it does; there is
    // nothing to ask for.
    .trigger = false,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& roster_fix_seam() noexcept {
  return roster_fix_definition;
}

}  // namespace amberfolio::machine
