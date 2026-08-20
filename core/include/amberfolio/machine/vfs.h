// SPDX-License-Identifier: AGPL-3.0-only
//
// The virtual filesystem: the interface under the DOS layer, and the DOS
// name semantics that make it possible for a backend to be dumb.
//
// platform.h reserves this file's place at the boundary and says why it is
// the one exception to "core to host is pulled": a DOS read handler
// (M2-D7, #52) cannot return until the bytes are there, so a file
// operation is a call core makes *outward*, answered before the handler
// resumes — the opposite direction from the frame, the audio and the
// console. `filesystem` below is that callout, in the same shape as
// `device` and `diagnostics`: an abstract interface core defines and a
// host (or, for M2's dev page and every test, this file's own in-memory
// backend, `memory_vfs.h`) implements.
//
//
// What lives here and why
// ------------------------
//
// Everything a backend must never be asked to decide lives in core:
//
//   * **Names are case-insensitive 8.3, canonicalized once.** A backend
//     sees `dos_name`/`dos_path` values that are already upper-cased,
//     already validated, already resolved through `.` and `..` — never a
//     raw string it would have to fold itself. That is the whole of what
//     the issue means by "a backend never makes a case-sensitivity
//     decision": if every backend folded case independently, a directory
//     backend on a case-sensitive host filesystem (Linux, most of macOS)
//     and the in-memory backend would disagree about whether `SAVE.DAT`
//     and `save.dat` are the same file, and that disagreement would be
//     silent until a replay depended on it.
//   * **There is one drive, C:, and paths are absolute or resolved
//     against a caller-supplied current directory.** `canonicalize()`
//     takes both and hands back one `dos_path`; the *storage* of "what is
//     the current directory right now" is DOS state and belongs to #52,
//     exactly as PLAN.md and this issue's tracking say — this file only
//     provides the pure function that turns a raw name plus a current
//     directory into a canonical path.
//   * **Errors are DOS error codes**, not `errno`, not a host's own
//     failure enum. `vfs_error` names the DOS 2.x extended-error-code
//     table entries this subset can produce (public documentation
//     — Ralf Brown's Interrupt List, or any MS-DOS programmer's
//     reference, states this table; nothing about it comes from or
//     resembles a Gold Box binary), and `dos_error_code()` is the one
//     place the two are tied together, so #52's handlers never invent a
//     number.
//   * **Enumeration order is name order, pinned.** PLAN.md §4 makes VFS
//     contents and their ordering part of a replay's initial conditions,
//     so "list the directory" cannot mean "whatever order a `std::map`
//     or a host's `readdir()` happens to hand back" — see `entry_at()`.
//
// What does **not** live here, on purpose:
//
//   * **The DOS handle table.** AH=3Dh hands a program a small integer
//     0-19ish and DOS owns the mapping from that integer to whatever this
//     interface's `open()` returned; that table is INT 21h state (#52).
//     `file_handle` here is this interface's own concern — identifying
//     one backend-side open file to `read`/`write`/`seek`/`close` — and a
//     backend is free to make it whatever is cheap for it (an index, in
//     `memory_vfs.h`'s case).
//   * **The current directory itself**, for the reason given above.
//   * **File attributes, timestamps, sharing modes.** The loader (#51)
//     needs to know a file exists and how big it is; nothing else in M2
//     reads anything else. `file_stat` carries exactly `size` and
//     `is_directory` and grows the day something needs more of it.
//
//
// Why the shapes are what they are
// ---------------------------------
//
// **Fixed-capacity types, not `std::string`/`std::vector`.** core/ is
// freestanding (PLAN.md §4): no allocation, no exceptions, none of
// `<string>`, `<vector>`, `<map>`, `<memory>`, `<functional>`. `dos_name`
// is a 12-byte char buffer plus a length (the longest a short name gets is
// `FILENAME.EXT`); `dos_path` is a fixed array of up to `max_depth` of
// them. Both compare with a defaulted `operator==` over the whole
// object, which only works — and is only worth doing — because every
// path into either type zeroes the unused tail before anything is
// compared; see the private layout notes on each type.
//
// **Results are a value plus a `vfs_error`, not an exception and not
// `std::expected`.** This is the same pattern `wall_clock::set()` and
// `memory_map::claim()` already use, generalized to the one case here
// that also wants to hand back a value on success: `vfs_result<T>`. A
// caller that ignores `.error` gets a zeroed `T`, the same discipline
// `attach()`'s ignored `bool` already has in this codebase.
//
// **`entry_at(dir, index)`, not "list the directory into a buffer".** The
// consumer this exists for is a FindFirst/FindNext pair (#52), which is
// itself index-based — DOS's own search record carries a position, not a
// cursor object — so the interface matches the shape its one caller
// needs instead of inventing an iterator abstraction with nothing to
// iterate with (core has no `<iterator>` machinery worth the name and no
// business building one). `entry_count()` first, then `entry_at()` per
// index, mirrors exactly how FindFirst asks "is there a first" and
// FindNext asks for the next index; #52 keeps the index, this interface
// stays stateless between calls.
//
//
// Rejected
// --------
//
// A single `list()` that fills a caller-supplied `std::span` of entries:
// it looks simpler until FindNext, which resumes an enumeration that may
// span many calls with other operations between them (the game can
// legally open a file mid-listing) — a snapshot-and-refill design would
// have to either re-list on every call (paying the sort more than
// `entry_at()` already does one entry at a time) or cache a listing
// somewhere, and "somewhere" is exactly the allocation core does not
// have. Per-index lookup needs no cache: the pinned order is a pure
// function of what currently exists, recomputed cheaply because nothing
// in M2's file counts is big enough for that to matter (`memory_vfs.h`
// has the numbers).
//
// A `filesystem` that takes raw path strings directly, doing its own
// canonicalization per call: it is what puts the case-sensitivity
// decision back in the backend, which is the one thing this design set
// out to prevent. `canonicalize()` is a free function precisely so a
// backend is never handed anything to canonicalize.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {

// --- DOS error codes ---------------------------------------------------

/// What a filesystem operation failed with, named for the DOS condition
/// it will be reported as rather than for anything backend-specific.
///
/// `none` is not a DOS code — every operation answers it on success, and
/// a caller checks `vfs_result<T>::ok()` (or compares directly) before
/// ever asking what the code would have been.
enum class vfs_error : std::uint8_t {
  none,
  /// DOS 02h. The named entry does not exist, but everything above it in
  /// the path does.
  file_not_found,
  /// DOS 03h. Some directory named on the way to the entry does not
  /// exist — and, in this interface, the answer for a syntactically
  /// illegal component too: a name no legal DOS short name can ever
  /// equal cannot be found by definition, so treating it as anything
  /// other than "the path does not resolve" would be inventing a
  /// distinction DOS's own ASCIZ path functions do not make. (The FCB
  /// functions have a dedicated invalid-filename outcome; the
  /// handle-based functions this subset backs do not, and use this
  /// code.)
  path_not_found,
  /// DOS 04h. Not DOS's own per-program handle table (#52 owns that) —
  /// this backend's own bound on files open at once, `too_many_open_files`
  /// on the same axis one level lower. See `memory_vfs.h::max_open_handles`.
  too_many_open_files,
  /// DOS 05h. The operation is refused for a reason that is not "it does
  /// not exist": opening or deleting a directory as if it were a file,
  /// creating a directory where a name is already taken, deleting a file
  /// with handles still open on it.
  access_denied,
  /// DOS 06h. `file_handle` named a slot this backend does not have open
  /// — already closed, or never opened by this backend at all.
  invalid_handle,
  /// DOS 0Fh. `canonicalize()` was given a drive letter other than C.
  /// There is one drive (PLAN.md §3); nothing needs a second code for
  /// "the drive letter was nonsense" versus "the drive letter was real
  /// but not this one" because this machine cannot tell them apart —
  /// neither one is C.
  invalid_drive,
  /// DOS 12h. `entry_at()` was asked for an index at or past
  /// `entry_count()`.
  no_more_files,
  /// DOS 52h, "cannot make directory entry". `create()`/`mkdir()` could
  /// not find a free slot in a backend's fixed-size entry table — see
  /// `memory_vfs.h::max_entries`.
  ///
  /// Deliberately not "disk full" (DOS 27h), and there is no code here
  /// for that at all. A backend that runs out of *bytes* — the in-memory
  /// one shares one arena between files, so it can — answers a short
  /// count from `write()` rather than an error, which is what DOS's own
  /// AH=40h does on a full disk and what `write()` already did at a
  /// file's size cap. Running out of entries is the only capacity that
  /// has to be reported as a failed operation, because there is no
  /// partial answer to "create this file".
  directory_full,
};

/// The AX value INT 21h's carry-set convention reports for `error`
/// (`service_floor.h::set_caller_carry`; #52 is where a handler actually
/// does this). `vfs_error::none` answers 0, which no DOS error function
/// call ever reports — a handler that reached here already checked `ok()`.
[[nodiscard]] constexpr std::uint16_t dos_error_code(vfs_error error) noexcept {
  switch (error) {
    case vfs_error::file_not_found:
      return 0x02;
    case vfs_error::path_not_found:
      return 0x03;
    case vfs_error::too_many_open_files:
      return 0x04;
    case vfs_error::access_denied:
      return 0x05;
    case vfs_error::invalid_handle:
      return 0x06;
    case vfs_error::invalid_drive:
      return 0x0F;
    case vfs_error::no_more_files:
      return 0x12;
    case vfs_error::directory_full:
      return 0x52;
    case vfs_error::none:
      break;
  }
  return 0;
}

/// A value plus how the operation that would have produced it went.
///
/// Same shape as `cpu::alu::result` (a value and the state that came with
/// it) generalized with an error rather than a flags word. `T{}` on
/// failure rather than an unspecified value, so a caller that forgets to
/// check `ok()` gets a defined, empty answer instead of whatever was left
/// on the stack — the same discipline an ignored `bool` return already
/// has elsewhere in this codebase (`memory_map::claim`, `machine::attach`).
template <class T>
struct vfs_result {
  T value{};
  vfs_error error{vfs_error::none};

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == vfs_error::none;
  }

  friend constexpr bool operator==(const vfs_result&,
                                   const vfs_result&) = default;
};

// --- DOS names and paths -------------------------------------------------

/// One canonicalized 8.3 component: up to eight name characters, a dot
/// only when there is an extension, up to three extension characters —
/// "FILENAME.EXT" at its longest, "FILENAME" when there is no extension.
/// Already upper-cased; this is the one spelling of a DOS short name a
/// backend is ever handed.
///
/// The unused tail of `chars_` is always zero: `parse()` is the only way
/// to produce a non-default `dos_name`, it never writes past `length_`,
/// and a default-constructed one is all zero. That is what lets
/// `operator==` be a plain member-wise default instead of a
/// length-then-compare written out by hand.
class dos_name {
 public:
  static constexpr std::size_t max_length = 12;

  constexpr dos_name() noexcept = default;

  /// Validate and canonicalize one path component read from a raw name a
  /// program or a host handed in — not yet split on `\`, not yet
  /// upper-cased, not yet checked. `vfs_error::path_not_found` for
  /// anything that is not a legal DOS short name: more than one dot, a
  /// name or extension too long, an empty name, or a character outside
  /// the DOS short-name set (letters, digits, and
  /// `! # $ % & ' ( ) - @ ^ _ \` { } ~`; that set, and the exclusion of
  /// space and the FAT-reserved punctuation, is the ordinary DOS/FAT
  /// short-name rule documented in any DOS technical reference — not
  /// anything specific to what this emulator runs).
  [[nodiscard]] static vfs_result<dos_name> parse(
      std::span<const char> raw) noexcept;

  [[nodiscard]] constexpr std::span<const char> text() const noexcept {
    return {chars_.data(), length_};
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return length_ == 0; }

  friend constexpr bool operator==(const dos_name&, const dos_name&) = default;

 private:
  std::array<char, max_length> chars_{};
  std::uint8_t length_{};
};

/// Lexicographic order over the canonical text, byte by byte, a shorter
/// name sorting before a longer one it is a prefix of. This is what
/// "name order" means everywhere this file says it — the pinned order
/// `entry_at()` walks — and it is exposed so a test (or #52, if a search
/// record ever wants to compare two names) does not have to restate it.
[[nodiscard]] constexpr bool dos_name_less(const dos_name& a,
                                           const dos_name& b) noexcept {
  const auto ta = a.text();
  const auto tb = b.text();
  const std::size_t n = (ta.size() < tb.size()) ? ta.size() : tb.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (ta[i] != tb[i]) {
      return ta[i] < tb[i];
    }
  }
  return ta.size() < tb.size();
}

/// A canonical absolute path below the one drive: a sequence of
/// `dos_name` components from the root, depth 0 meaning the root itself
/// (`C:\`). Never carries a drive letter or a `\` — those are surface
/// syntax `canonicalize()` consumes, not part of the canonical value.
///
/// As with `dos_name`, the unused tail of `components_` is always a
/// default `dos_name{}`: `push()` is the only way to grow a path and
/// `pop()` clears the slot it vacates (rather than merely decrementing
/// `depth_`), which is what lets `operator==` be a plain default over
/// the whole fixed array instead of a depth-then-compare.
class dos_path {
 public:
  /// Directories deep a canonical path may go, the drive and the final
  /// name not counted — `SAVE\SLOT01\CHAR1.DAT` is depth 3. A Gold Box
  /// installation is close to flat (its files sit at the drive root, with
  /// at most a save-data subdirectory below it); eight is generous
  /// headroom over that without a path deep enough that this fixed array
  /// becomes the thing a reviewer has to reason about.
  static constexpr std::size_t max_depth = 8;

  constexpr dos_path() noexcept = default;

  [[nodiscard]] constexpr std::size_t depth() const noexcept { return depth_; }

  [[nodiscard]] constexpr bool is_root() const noexcept { return depth_ == 0; }

  /// Component `index`, counting from the root. `index < depth()` is a
  /// precondition; there is no defined component past the depth a path
  /// actually has.
  [[nodiscard]] constexpr const dos_name& component(
      std::size_t index) const noexcept {
    return components_[index];
  }

  /// The last component. `!is_root()` is a precondition — the root has
  /// no leaf, which is exactly why `open()`/`create()` refuse it
  /// (`vfs.h`'s `filesystem` interface below).
  [[nodiscard]] constexpr const dos_name& leaf() const noexcept {
    return components_[depth_ - 1];
  }

  /// The path with its last component removed. The root's parent is the
  /// root — there is nowhere further up, the same rule `canonicalize()`
  /// gives `..` at the root.
  [[nodiscard]] constexpr dos_path parent() const noexcept {
    dos_path result = *this;
    result.pop();
    return result;
  }

  /// Append `name`. False, and the path unchanged, at `max_depth` — the
  /// one way this type can fail to represent something, and it answers
  /// rather than silently truncating (PLAN.md §3's "log, don't fake",
  /// one layer down: a truncated path would resolve to a *different*,
  /// real location instead of failing).
  constexpr bool push(const dos_name& name) noexcept {
    if (depth_ >= max_depth) {
      return false;
    }
    components_[depth_++] = name;
    return true;
  }

  /// Remove the last component. False, and nothing changed, at the root.
  constexpr bool pop() noexcept {
    if (depth_ == 0) {
      return false;
    }
    components_[--depth_] = dos_name{};
    return true;
  }

  friend constexpr bool operator==(const dos_path&, const dos_path&) = default;

 private:
  std::array<dos_name, max_depth> components_{};
  std::uint8_t depth_{};
};

/// Resolve a raw path a program or a host handed in against
/// `current_directory` into a canonical `dos_path`.
///
/// `raw` may be:
///   * Absolute (`\GAME\SAVE1.DAT`) or drive-absolute (`C:\GAME\SAVE1.DAT`
///     — case-insensitively; only `C` is accepted, everything else is
///     `vfs_error::invalid_drive`), in which case `current_directory` is
///     not consulted at all.
///   * Relative (`SAVE1.DAT`, `..\OTHER`), resolved against
///     `current_directory` component by component.
///   * Empty, which resolves to `current_directory` unchanged — the DOS
///     reading of "no path given".
///
/// `.` is a no-op component and `..` removes one component, both
/// including at the root, where `..` leaves the path at the root exactly
/// as real DOS does (there is nothing above `C:\` to ascend to). Repeated
/// or trailing `\` collapse away rather than producing an empty
/// component — `A\\B` and `A\B\` both mean what `A\B` means.
///
/// Each non-`.`/`..` component is validated and upper-cased by
/// `dos_name::parse()`; the first component that fails ends the whole
/// resolution with that failure's error, because a path is either fully
/// legal or it does not resolve at all — there is no such thing as
/// "resolve as far as possible".
[[nodiscard]] vfs_result<dos_path> canonicalize(
    const dos_path& current_directory, std::span<const char> raw) noexcept;

// --- The filesystem interface -------------------------------------------

/// A backend-assigned identifier for one open file, opaque outside the
/// backend that issued it — an index into `memory_vfs.h`'s handle table,
/// a native file descriptor for a directory-backed host (#54), whatever
/// is cheapest for the backend to look up again. **Not** the DOS handle a
/// program sees in AX after AH=3Dh; see this file's top comment.
struct file_handle {
  std::uint32_t slot{0xFFFFFFFFU};

  friend constexpr bool operator==(const file_handle&,
                                   const file_handle&) = default;
};

/// The answer of every unopened `file_handle` and of a `vfs_result`
/// that failed — a caller comparing against this rather than trusting an
/// unchecked `.value` is making the same mistake `vfs_result::ok()`
/// exists to make unnecessary, but the sentinel is still here because a
/// default-constructed `file_handle` has to mean *something*, and
/// "invalid" is the only honest something.
inline constexpr file_handle invalid_file_handle{};

/// `AH=3Dh`'s AL, access-mode bits: 0 read, 1 write, 2 both. Named and
/// valued to match so a handler that decodes AL can `static_cast` it
/// directly rather than translating through a second table.
enum class open_mode : std::uint8_t {
  read_only = 0,
  write_only = 1,
  read_write = 2,
};

/// `AH=42h`'s AL, seek origin: 0 from the start, 1 from the current
/// position, 2 from the end. Same reasoning as `open_mode`.
enum class seek_origin : std::uint8_t {
  begin = 0,
  current = 1,
  end = 2,
};

/// What the loader (#51) and a directory listing need to know about one
/// path, and nothing this subset does not use — see this file's top
/// comment for what is deliberately not here.
struct file_stat {
  std::uint32_t size{};
  bool is_directory{};

  friend constexpr bool operator==(const file_stat&,
                                   const file_stat&) = default;
};

/// One answer from `entry_at()`: a name in `entry_count()`'s directory,
/// and the same two facts `stat()` would give that name.
struct directory_entry {
  dos_name name;
  std::uint32_t size{};
  bool is_directory{};

  friend constexpr bool operator==(const directory_entry&,
                                   const directory_entry&) = default;
};

/// The virtual filesystem under the DOS layer (PLAN.md §3-4): open,
/// create, read, write, seek, close, unlink, mkdir — the INT 21h subset
/// this machine backs — plus `exists`/`stat` for the loader and
/// `entry_count`/`entry_at` for FindFirst/FindNext.
///
/// Abstract in the same shape as `device` and `diagnostics`: no copy, no
/// move, a protected non-virtual destructor because callers hold this by
/// reference and never own or delete it through this type. Every
/// concrete backend — `memory_vfs.h`'s `memory_filesystem`, the
/// directory-backed host (#54), the IndexedDB-backed one (#55) — takes
/// only canonical `dos_path` values and answers only `vfs_error`, and
/// this is the entire contract between them: see this file's top comment
/// for why the split lands exactly here.
///
/// Mutating operations are not `[[nodiscard]]`, matching `memory_map::
/// claim` and `machine::attach` elsewhere in this codebase — the query
/// operations (`exists`, `stat`, `entry_count`, `entry_at`) are, because
/// they are pure accessors and calling one for its side effect (there is
/// none) would always be a mistake.
class filesystem {
 public:
  filesystem() = default;
  filesystem(const filesystem&) = delete;
  filesystem(filesystem&&) = delete;
  filesystem& operator=(const filesystem&) = delete;
  filesystem& operator=(filesystem&&) = delete;

  /// Open an existing file. `vfs_error::access_denied` if `path` names a
  /// directory or is the root — this interface has no directory handles,
  /// because nothing in M2's INT 21h subset opens one.
  virtual vfs_result<file_handle> open(const dos_path& path,
                                       open_mode mode) = 0;

  /// Create `path`, truncating it to empty if it already exists as a
  /// file (DOS 3Ch's own behaviour — creation is not an error just
  /// because the name is taken), opened `read_write` either way.
  /// `vfs_error::access_denied` if `path` names an existing directory or
  /// is the root.
  virtual vfs_result<file_handle> create(const dos_path& path) = 0;

  /// Read up to `out.size()` bytes at the handle's current position,
  /// advancing it by however many were actually read. Zero at end of
  /// file is success, not `vfs_error::none`'s absence — real DOS answers
  /// the same way.
  virtual vfs_result<std::size_t> read(file_handle handle,
                                       std::span<std::uint8_t> out) = 0;

  /// Write up to `in.size()` bytes at the handle's current position,
  /// advancing it by however many were actually written. A short write —
  /// fewer bytes written than asked for, possibly zero — is success, the
  /// same "ran out of room, said so honestly in the count" answer real
  /// DOS gives for a full disk; a backend's capacity is not a program's
  /// concern.
  virtual vfs_result<std::size_t> write(file_handle handle,
                                        std::span<const std::uint8_t> in) = 0;

  /// Move the handle's position. The new position is always clamped to
  /// zero at the low end (DOS's own behaviour here is not consistent
  /// across versions for a seek that would go negative; clamping is the
  /// simplest answer that is never observably wrong for a program that
  /// does not rely on the inconsistency) and, in this backend and every
  /// one built to this interface, at the high end by whatever capacity
  /// the backend has — a subsequent `write` past the old end of file
  /// reads back as zero for the gap, never as leftover bytes from
  /// something else that once lived at that offset (PLAN.md §4:
  /// determinism includes what a backend has not been told to write).
  virtual vfs_result<std::uint32_t> seek(file_handle handle, seek_origin origin,
                                         std::int32_t offset) = 0;

  /// Set the file's length to the handle's current position — DOS's own
  /// AH=40h write-with-CX=0000h convention (M2-D7, #52): shrinks if the
  /// position is behind the old end, extends with zero fill (`write()`'s
  /// own gap rule, above) if it is ahead. `vfs_error::invalid_handle` if
  /// `handle` is not open, `vfs_error::access_denied` for a handle opened
  /// `read_only` — the same guard `write()` has, because DOS counts this
  /// as a write.
  virtual vfs_error truncate(file_handle handle) = 0;

  /// Release the handle. `vfs_error::invalid_handle` if it was not open.
  virtual vfs_error close(file_handle handle) = 0;

  /// Delete a file. `vfs_error::access_denied` for a directory, the root,
  /// or a file with a handle still open on it — this interface has no
  /// "delete on last close"; a caller closes first.
  virtual vfs_error unlink(const dos_path& path) = 0;

  /// Create a directory. `vfs_error::access_denied` if `path` already
  /// names anything, `vfs_error::path_not_found` if its parent does not
  /// exist, `vfs_error::directory_full` if the backend has no room for
  /// another entry.
  virtual vfs_error mkdir(const dos_path& path) = 0;

  /// Whether anything answers to `path` — a file, a directory, or the
  /// root. No error channel: a malformed or absent path both simply do
  /// not exist, which is a complete answer on its own (the loader's one
  /// use for this, #51, wants exactly a yes/no).
  [[nodiscard]] virtual bool exists(const dos_path& path) const = 0;

  /// Size and kind of `path`. `vfs_error::file_not_found` /
  /// `path_not_found` exactly as a failed `open()` would report, so a
  /// caller that already knows how to read one error convention does not
  /// need a second.
  [[nodiscard]] virtual vfs_result<file_stat> stat(
      const dos_path& path) const = 0;

  /// How many entries `dir` directly contains. `vfs_error::path_not_found`
  /// if `dir` does not exist, `vfs_error::access_denied` if it names a
  /// file rather than a directory.
  [[nodiscard]] virtual vfs_result<std::size_t> entry_count(
      const dos_path& dir) const = 0;

  /// The `index`-th entry of `dir` in name order (`dos_name_less`),
  /// pinned so that a replay's directory listing never depends on
  /// anything but what currently exists (PLAN.md §4). Same errors as
  /// `entry_count()` for a bad `dir`; `vfs_error::no_more_files` for
  /// `index >= entry_count(dir)`.
  [[nodiscard]] virtual vfs_result<directory_entry> entry_at(
      const dos_path& dir, std::size_t index) const = 0;

 protected:
  // See device.h / diagnostics.h: held by reference, never deleted
  // through this type.
  ~filesystem() = default;
};

}  // namespace amberfolio::machine
