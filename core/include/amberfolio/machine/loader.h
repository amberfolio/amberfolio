// SPDX-License-Identifier: AGPL-3.0-only
//
// The MZ loader: load a real-mode EXE off the VFS into conventional
// memory the way DOS does, so that in M3 the game's own unpacker starts
// from a state it recognizes (PLAN.md §7 M2, issue #51).
//
//
// What DOS actually does, and what this does instead
// -----------------------------------------------------
//
// Real DOS is a multi-program, MCB-chained allocator: EXEC (INT 21h
// AH=4Bh) walks a chain of memory-control blocks looking for a fit,
// builds an environment block by copying the parent's, and chains the
// new PSP's parent field to whoever called EXEC. None of that exists
// here, on purpose (PLAN.md §3's scope and this issue's own text): this
// is a **single-program machine**. There is never a second program to
// leave room for, never a parent process to chain to, and never an
// allocation to free — so the loader always does the same three things,
// in the same place, every time:
//
//   * The PSP goes at `psp_load_segment`, a fixed constant, not "the
//     first free block a chain walk finds."
//   * The program gets every paragraph of conventional memory above its
//     own image — the "MAXALLOC-style default" real DOS gives a program
//     linked with MAXALLOC=0FFFFh (the overwhelming majority of them),
//     applied unconditionally rather than read out of the header. The
//     header's actual MAXALLOC is parsed and never consulted for
//     placement; MINALLOC is still honored, because it is the program's
//     own statement of what it cannot run without.
//   * There is no MCB chain, so DOS's allocation functions — 48h
//     (allocate), 49h (free), 4Ah (resize) — have nothing to walk. They
//     are not in PLAN.md §3's INT 21h subset, and #52 hits the
//     log-don't-fake path for them: whether the game ever calls one is
//     exactly the fact that log line exists to surface.
//
//
// Malformed input is a diagnostic, not UB
// -----------------------------------------
//
// The bytes this loader reads come off the VFS, which in production is a
// player's own copy of a decades-old file — no reason to trust it any
// more than a corrupt download. Every field taken from the file is
// bounds-checked against the file's own length (`vfs::file_stat::size`,
// fetched once up front) *before* anything is read using it: the image
// length, the header size, and the relocation table's extent are all
// range-checked against that one number before a single byte past the
// fixed 28-byte header is touched. A malformed file answers a
// `loader_error` through `loader_result<T>` (`vfs_result<T>`'s own
// shape, generalized the same way `vfs.h` generalizes it); it never
// reads past what `fs.stat()` said was there and it never indexes memory
// with an unchecked value. Indexing `memory_map::ram()` itself cannot be
// UB regardless — `cpu::physical_address()` always masks to 20 bits — so
// every check here is about catching a *wrong* load, not an unsafe one.
//
//
// Relocations, and the one case that catches a shortcut
// ---------------------------------------------------------
//
// This is the loader's whole reason to exist: an MZ image is linked as
// if it were loaded at segment 0, and every place the linker could not
// resolve without knowing the real load segment — every far pointer's
// segment half — gets a table entry instead. Applying one is "read a
// word, add the load segment, write it back," and the one way to get
// that step subtly wrong is to address the word by physical-address
// arithmetic (`addr`, `addr + 1`) instead of by segment:offset the way
// the processor itself would (`cpu::processor`'s own comment: offsets
// wrap at 64 KiB *within the segment*). The two agree for every offset
// except 0xFFFF, where the high byte of the word is offset 0x0000 of the
// *same* segment — 65535 bytes behind the low byte, not one physical
// byte ahead of it. `apply_relocation()` below uses
// `cpu::physical_address(segment, offset)` for each half separately,
// with the high half's offset formed by a `std::uint16_t` addition that
// wraps on its own, rather than adding 1 to a physical address; the test
// suite exercises exactly the 0xFFFF case; a version that used the
// naive arithmetic would patch the wrong sixteen bits of memory and pass
// every other fixture.
//
// A relocation's target is checked against the memory the program was
// granted — `psp_load_segment` through the top of conventional memory —
// rather than against the narrower loaded-image range. Real DOS does not
// check this at all, trusting the linker completely; this loader adds
// the one bound that catches a hostile or corrupt table without
// second-guessing a well-formed one, since a linker is free to place a
// relocation anywhere in the program's own granted memory (a BSS-like
// region past the raw image, for one) and not only inside the bytes the
// file actually carried.
//
//
// The PSP: what is honestly fillable in M2, and what is not
// --------------------------------------------------------------
//
// A real PSP has fields this milestone has no truthful value for: the
// parent's PSP segment (there is no parent process), the environment
// block's segment (this loader builds none), the saved INT 22h/23h/24h
// vectors, the job file table, the default FCBs. All of it is left
// zero rather than invented — the machine layer's own "log, don't fake"
// rule, one level down from a service call: a zero the game's startup
// code chokes on is a fast, legible failure, and a fabricated value that
// happens to work today is the kind of thing this project's discipline
// exists to prevent. `psp` namespace below documents which of these M3
// (or #52's AH=50h/51h) may eventually need to force a real value into.
//
// What *is* filled honestly, because every DOS program can read it on
// its first instruction and this machine can answer truthfully: INT 20h
// at offset 0 (a real two-byte instruction, not a marker — a program
// that falls off the end of its own code into the PSP terminates
// instead of executing whatever garbage follows, a real DOS-era trick),
// the top-of-memory word, and the command tail.
//
//
// Rejected
// --------
//
// A loader that reads the whole file into a scratch buffer first: core/
// is freestanding (PLAN.md §4) — no `<vector>`, no allocation — and the
// image has nowhere to live but its own place in `memory_map::ram()`
// anyway, so `load_program()` reads directly into that span. The 28-byte
// header and each 4-byte relocation entry are the only things read into
// a stack buffer, because they have to be decoded before their own
// bytes' final home in memory is known.
//
// A loader that clamps `MAXALLOC` to the header's own value: it is
// meaningless in a single-program machine (there is nothing else that
// memory could ever be given to), and clamping to it would make this
// loader's behaviour depend on a linker default nobody chose for a
// reason that matters here.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

class machine;

// --- Placement -------------------------------------------------------

/// One paragraph: 16 bytes, the 8086's own unit for segment arithmetic
/// and the unit every MINALLOC/MAXALLOC/header-size field in this file
/// is stated in.
inline constexpr std::uint32_t paragraph_size = 16;

/// The lowest segment a loaded program's PSP may occupy. Everything
/// below it is the machine's own resident memory, laid down at power-on
/// by `service_floor.h` and never the loader's to touch: the interrupt
/// vector table at 0000:0000-0000:03FF (`cpu::interrupts.h`) and the
/// BIOS data area at 0040:0000-0040:00FF (`bda`, `service_floor.h`),
/// which physically ends at 0x4FF. A real DOS also keeps its own kernel
/// and command interpreter resident below the first program it loads;
/// this machine has none of that — the DOS/BIOS layer is native C++ that
/// never occupies emulated memory (PLAN.md §3) — so the floor here is
/// exactly the service floor's own reservation, rounded up to the next
/// free paragraph: physical 0x500, segment 0x0050.
inline constexpr std::uint16_t resident_floor_segment = 0x0050;

/// Where the PSP goes: fixed, at the floor, because there is never a
/// second program to leave room above the first for (PLAN.md §3).
inline constexpr std::uint16_t psp_load_segment = resident_floor_segment;

/// The PSP's size in paragraphs — 256 bytes, DOS reality rather than a
/// choice this project made.
inline constexpr std::uint32_t psp_paragraphs = 0x10;

/// Where the program image goes: PSP+10h, DOS's own fixed offset for
/// every real-mode EXE.
inline constexpr std::uint16_t image_load_segment =
    static_cast<std::uint16_t>(psp_load_segment + psp_paragraphs);

// --- The PSP layout ----------------------------------------------------

/// Offsets into the 256-byte PSP this loader writes, and the reasoning
/// for what it does and does not fill — see this file's top comment.
namespace psp {

/// The PSP's fixed size. Every byte outside what is named below is left
/// zero.
inline constexpr std::uint16_t size = 0x100;

/// CD 20: a real INT 20h instruction, not a marker. See this file's top
/// comment on why it has to actually be one.
inline constexpr std::uint16_t int20_offset = 0x00;

/// The segment one past the last paragraph allocated to the program —
/// "top of memory" in DOS's own terms. INT 12h and a program's own
/// memory-sizing code both read it; here it is always
/// `conventional_ram_size / paragraph_size` (memory_map.h), because
/// placement always grants everything above the image (this file's top
/// comment).
inline constexpr std::uint16_t top_of_memory_offset = 0x02;

/// The parent's PSP segment. Real DOS chains this to whatever called
/// EXEC; this machine has no parent process, so it is the documented
/// placeholder 0000h. #52's AH=50h/51h (get/set PSP) and M3 are the
/// only things that could ever need a real value here, and only if a
/// game's own startup code reads and validates it — which would be the
/// first sign that it matters.
inline constexpr std::uint16_t parent_offset = 0x16;

/// The environment block's segment. Real DOS builds one from the
/// parent's own and the program's `SET` variables; this machine builds
/// none, so this is the documented placeholder 0000h. Forced only if
/// M3's game refuses to run without one — Gold Box's era rarely does.
inline constexpr std::uint16_t environment_offset = 0x2C;

/// The command tail's length byte, at DOS's own fixed offset.
inline constexpr std::uint16_t command_tail_count_offset = 0x80;

/// The command tail's bytes, immediately after the count.
inline constexpr std::uint16_t command_tail_bytes_offset = 0x81;

/// The longest tail this PSP can hold: the 128-byte region 80h-FFh minus
/// the count byte and the trailing CR every tail ends with, DOS's own
/// arithmetic for this field since the first PSP.
inline constexpr std::size_t command_tail_max_length = 126;

}  // namespace psp

// --- The MZ header -------------------------------------------------------

/// The fixed part of every MZ header, and the format facts this loader
/// is built against — a DOS technical reference states all of them;
/// none of it is specific to what this emulator runs.
namespace mz {

/// The 28-byte fixed header every MZ file has, at these exact offsets,
/// before whatever else `header_paragraphs` says the header holds (this
/// loader reads no more of it — nothing past these fields is part of
/// M2's INT 21h subset).
inline constexpr std::size_t encoded_size = 0x1C;

/// "MZ", the signature every DOS EXE loader accepts, read as a
/// little-endian word at offset 0. ("ZM" is a documented historical
/// alternate this project's one known toolchain never produces, so it is
/// not honored — accepting it would be a decision with nothing to test
/// it against.)
inline constexpr std::uint16_t signature = 0x5A4D;

/// Bytes in one page of the image-length calculation — not a memory
/// page, a fact of this file format's own units.
inline constexpr std::uint32_t page_size = 512;

/// Bytes in one relocation table entry: offset, then segment.
inline constexpr std::uint32_t relocation_entry_size = 4;

}  // namespace mz

/// Why an MZ file failed to load. Every one of these is something a
/// hostile or merely corrupt file can trigger; none of them is this
/// loader losing track of an invariant it owns (that would be a bug, not
/// a `loader_error`).
enum class loader_error : std::uint8_t {
  none,
  /// The file is shorter than `mz::encoded_size`, or its first word is
  /// not `mz::signature`.
  bad_signature,
  /// `page_count` is zero, `last_page_size` exceeds `mz::page_size`, or
  /// the image length the two of them compute (the last-page-size rule)
  /// does not fit inside the file `fs.stat()` reported.
  bad_image_length,
  /// `header_paragraphs * paragraph_size` is smaller than
  /// `mz::encoded_size` (the header cannot be shorter than its own fixed
  /// part) or larger than the image length just computed (there would be
  /// no load module left at all).
  bad_header_size,
  /// The relocation table — `relocation_table_offset` for
  /// `relocation_count * mz::relocation_entry_size` bytes — runs past
  /// the header.
  bad_relocation_table,
  /// A relocation entry, once shifted by the load segment, patches a
  /// word outside the memory the program was granted (`psp_load_segment`
  /// through the top of conventional memory).
  relocation_out_of_image,
  /// The load module (image length minus header size) does not fit
  /// between `image_load_segment` and the top of conventional memory.
  image_too_large,
  /// What is left of conventional memory above the image is fewer than
  /// `min_alloc` paragraphs — the program's own stated minimum.
  insufficient_memory,
  /// The relocated initial CS:IP or SS:SP lands outside conventional
  /// memory.
  bad_entry_point,
  /// `command_tail` is longer than `psp::command_tail_max_length`.
  command_tail_too_long,
  /// Opening, seeking, closing or reading the file through `fs` itself
  /// reported a `vfs_error` — a VFS-layer failure rather than anything
  /// about the bytes of the file.
  file_error,
};

/// Same shape as `vfs_result<T>` (vfs.h), over `loader_error` instead of
/// `vfs_error`: a value plus how the operation that would have produced
/// it went, `T{}` on failure so a caller that forgets to check `ok()`
/// gets a defined, empty answer rather than whatever was left behind.
template <class T>
struct loader_result {
  T value{};
  loader_error error{loader_error::none};

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == loader_error::none;
  }
};

/// The MZ header, decoded from its 28 fixed bytes and range-checked
/// against nothing but itself — `decode()` cannot know the file's total
/// length or the relocation table's actual bytes, both of which
/// `load_program()` checks once it does.
struct mz_header {
  std::uint16_t last_page_size{};
  std::uint16_t page_count{};
  std::uint16_t relocation_count{};
  std::uint16_t header_paragraphs{};
  std::uint16_t min_alloc{};
  std::uint16_t max_alloc{};
  std::uint16_t initial_ss{};
  std::uint16_t initial_sp{};
  std::uint16_t checksum{};
  std::uint16_t initial_ip{};
  std::uint16_t initial_cs{};
  std::uint16_t relocation_table_offset{};
  std::uint16_t overlay_number{};

  /// Decode the fixed header from `raw`, which must be exactly
  /// `mz::encoded_size` bytes — the caller's job to have read that many
  /// (it is what `load_program()` uses this for). Checks the signature
  /// only; `image_length()` and `header_size()` below are their own
  /// functions because they need facts `decode()` alone does not have.
  [[nodiscard]] static loader_result<mz_header> decode(
      std::span<const std::uint8_t> raw) noexcept;

  /// The image length in bytes: the last-page-size rule applied to
  /// `page_count`/`last_page_size` (this issue's own name for it).
  /// `last_page_size == 0` means the last page is full, not empty — an
  /// image of `page_count == 0` has no legal length at all.
  [[nodiscard]] loader_result<std::uint32_t> image_length() const noexcept;

  /// `header_paragraphs * paragraph_size`, unconditionally — a plain
  /// widening multiply, never truncated.
  [[nodiscard]] constexpr std::uint32_t header_size() const noexcept {
    return static_cast<std::uint32_t>(header_paragraphs) * paragraph_size;
  }
};

// --- The load ------------------------------------------------------------

/// What a successful load leaves behind: the segments a caller needs to
/// know about beyond what `load_program()` already wrote into the
/// processor's registers — `load_segment` is what a debugger or a future
/// seam's fingerprint lookup wants, and the rest restate the entry state
/// for a caller that would rather read it here than out of `machine::
/// processor()`.
struct loaded_program {
  std::uint16_t psp_segment{};
  std::uint16_t load_segment{};
  std::uint16_t entry_cs{};
  std::uint16_t entry_ip{};
  std::uint16_t entry_ss{};
  std::uint16_t entry_sp{};
};

/// Load `path` off `fs` into `box`'s conventional memory the way DOS
/// does, and leave the processor ready to run it.
///
/// On success: the PSP is written at `psp_load_segment`, the image is
/// placed and relocated at `image_load_segment`, `box.services()` has
/// INT 20h wired to terminate the run (the PSP's own INT 20h, and
/// `machine::exit_program()` is what both it and #52's AH=4Ch call —
/// this file's top comment on the exit path), and the processor holds
/// the entry state a real DOS EXEC leaves: DS=ES=PSP segment, CS:IP and
/// SS:SP relocated from the header, AX=0000h (the FCB drive-validity
/// convention no FCB in this machine ever contradicts), flags clean
/// (`cpu::processor::reset()` is called first, so BX/CX/DX/BP/SI/DI are
/// zero too — real DOS leaves them undefined, and zero is the one value
/// among "undefined" that cannot make a replay nondeterministic).
///
/// On failure, nothing above has happened: a `loader_error` explains
/// why, and neither the processor nor conventional memory above the
/// service floor were touched.
///
/// `command_tail` is copied into the PSP's 80h-FFh region verbatim — a
/// caller strips whatever leading characters DOS's own command-line
/// parsing would have (there is none of that parsing here to duplicate,
/// PLAN.md §3's scope). Longer than `psp::command_tail_max_length` is
/// `loader_error::command_tail_too_long`.
[[nodiscard]] loader_result<loaded_program> load_program(
    machine& box, filesystem& fs, const dos_path& path,
    std::span<const char> command_tail = {});

}  // namespace amberfolio::machine
