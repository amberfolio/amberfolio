// SPDX-License-Identifier: AGPL-3.0-only
//
// The on-screen keyboard, as data: which keys are on it, where they sit,
// what each one produces, and what moves the focus between them (#377).
//
// A phone has no keyboard, and this game asks for a character's name. It
// also answers Y or N a dozen times an hour and takes single-letter
// commands off its own bars, so a touch device that cannot type is a
// touch device that cannot play — which is exactly what PLAN.md §7's
// exit for M6 rules out ("fully playable, text entry included, on a
// device with no keyboard attached to it").
//
// The keyboard that fills that gap is a *widget*, and a widget belongs
// in a host. What is here is the part that is not: the tables it is
// drawn from and the rules it is driven by. PLAN.md §4 draws that line
// for the whole of this input work — "the layout and navigation model
// are shared", each host presenting the overlay its own way — and the
// reason is the ordinary one for this repository: a model that lived in
// `hosts/web/page/app.mjs` would have to be either imported by the
// desktop host, which cannot import a DOM, or restated there by hand,
// which is two answers to one question. A layout change should be a
// change to this file and to nothing else.
//
//
// What crosses the boundary is a scan code, and nothing else
// -----------------------------------------------------------
//
// Every key here carries one XT set-1 make code, and a host that commits
// a key posts exactly that through `machine::post_key()`
// (`af_machine_post_key`). No new key concept enters the machine: the
// BIOS cannot tell a key committed on a painted keyboard from a key
// struck on a real one, because there is nothing different about it to
// tell. What a scan code *means* — which character, which shift rule,
// which BDA bit — stays where it already is, in `keyboard.h`, and this
// file reads that table rather than restating any of it.
//
// The keys are therefore the machine's own 83, not a modern board's: the
// `full` layout below carries scan codes 0x01 through 0x53 exactly once
// each, which is every key an IBM PC/XT keyboard has, and
// `screen_keyboard_test.cpp` derives that against `xt_keyboard::xt_table`
// rather than trusting the list. Nothing on this keyboard is unreachable,
// because a keyboard with a missing key is a game with a missing command.
//
//
// The held-key contract, stated because #377 asked for it stated
// ----------------------------------------------------------------
//
// **There is no key repeat.** Holding a finger on a painted key produces
// one keystroke, not a stream. Nothing in this machine repeats a key —
// there is no IRQ 1 and no typematic timer (`keyboard.h`), the BIOS's
// buffer is filled from make codes alone — so a repeat would have to be
// invented by a host, and a host inventing input is the same fault as a
// host inventing a port's answer.
//
// **A modifier latches; everything else taps.** A finger cannot hold
// Shift and press A, so Shift, Ctrl and Alt are *latching* keys: a
// commit puts one down and it stays down, a second commit lets it go,
// and committing any ordinary key sends that key and then lets every
// latched modifier go behind it. That is `commit_key()` below, and it is
// here rather than in each host for the reason everything else here is:
// the order of those events is load-bearing. The modifier's make has to
// reach the BDA's shift-flag byte before the letter's does, because a
// program reads 40:17 directly (`platform.h`) and would otherwise see
// the letter arrive unshifted.
//
// The lock keys are *not* latching. Caps Lock, Num Lock and Scroll Lock
// toggle inside the BIOS on the make code, so a tap is already what they
// want, and treating one as a latch would leave the key physically down
// and toggle it a second time when it came up.
//
//
// Three layouts, because the program asks three kinds of question
// -----------------------------------------------------------------
//
// A layout is chosen by what the program is waiting for, and the three
// are not the same size or the same shape:
//
//   * `prompt` — the yes/no question, and the page of text waiting to be
//     dismissed. Four keys on one row.
//   * `name` — free text: a character's name. Letters, digits, a shift,
//     a space, a backspace and a return, and nothing else to hit by
//     accident.
//   * `full` — the machine's whole keyboard, for everything the other
//     two do not carry.
//
// The three were derived from the screens `docs/playable.md` documents
// rather than from a generic keyboard: the legs there press letters, the
// digits, Return, Escape, Tab and the movement cluster, and every one of
// those is reachable in at least one layout.
//
// **Which layout is up is the host's choice, and this build makes it by
// asking the player.** Nothing here watches the program to see what it
// is waiting for, because nothing outside the seam engine may look
// (PLAN.md §4's fidelity boundary), and a guess dressed as an answer is
// what this project refuses everywhere else. A seam that knew the prompt
// could choose one later; until then the honest arrangement is three
// buttons and a person.
//
//
// Geometry, in quarter units
// ---------------------------
//
// A layout is rows of keys. Every row is one unit tall, and a key's
// width and its offset along its row are in *quarter* units, so that a
// shift two keys wide and a return two-and-a-quarter are both
// expressible without anybody inventing a fraction. `unit` is that four.
//
// Nothing here is in pixels, and nothing here is a picture of a physical
// keyboard: the function keys sit in a row rather than in the
// two-by-five block an XT has them in, because this is a grid a finger
// has to hit rather than a photograph of a board nobody watching is
// holding. A host picks a key size, multiplies, and draws.
//
//
// Focus, and what moves it
// -------------------------
//
// One key is focused. A pointer or a finger sets the focus by landing on
// a key and commits it in the same gesture; `key_at()` is that hit test
// for a host drawing into a framebuffer, where there is no widget under
// the pointer to ask. A host whose keys are real widgets — the page's are
// buttons — positions them from the columns and widths below and lets its
// own toolkit do the hitting, and a gap is still a gap in both, because
// both put the keys in the same places. A four-way control — arrow keys
// today, a gamepad's stick or d-pad in M8 (#210) — moves the focus with
// `move()` and commits it separately.
//
// The focus itself is **not here**. It is one integer, it is UI state,
// and it belongs to whichever host is drawing: `move()` is a pure
// function of a layout, a focus and a direction, the way
// `match_save_file()` is a pure function of a table and a path. Core
// keeps no on-screen keyboard state at all, which is also what keeps all
// of this out of the serialization and out of a recording's checkpoints
// — there is nothing to leave out.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace amberfolio::machine::screen_keyboard {

/// Quarter units in one unit — the width of an ordinary key, and the
/// height of every row. A host multiplies by whatever a key is worth in
/// pixels on the screen it has.
inline constexpr std::uint8_t unit = 4;

/// Which modifier a key latches, as a bit in the mask a host carries
/// between commits. `none` is every other key, which taps.
///
/// A mask and not a single choice, because a player may latch Shift and
/// Ctrl together and the machine has a bit for each of them at 40:17.
///
/// The two shifts have a bit each, though the BDA gives them one flag
/// between them: what the mask is for is knowing which key to let go of
/// again, and letting go of the shift that was never pressed would leave
/// the one that was held down for the rest of the run.
enum class latch : std::uint8_t {
  none = 0x00,
  left_shift = 0x01,
  right_shift = 0x02,
  ctrl = 0x04,
  alt = 0x08,
};

/// One painted key: the legend on it, the make code it produces, and
/// where it sits.
struct key {
  /// What is printed on it — `A`, `Esc`, `8 Up`. Upper case for the
  /// letters, because that is the legend on the keycap and because the
  /// game's own commands are upper case (`docs/seams.md` §10).
  std::string_view label;
  /// The XT set-1 make code, 0x01-0x53. The break code is this with bit
  /// 7 set, which is `machine::post_key()`'s business and not spelled
  /// here.
  std::uint8_t scancode{};
  /// Which row it is on, counting from the top.
  std::uint8_t row{};
  /// Quarter units from the left edge of the *layout* — not of the row,
  /// so that a short row can be centred by starting it further in.
  std::uint8_t column{};
  /// Quarter units wide.
  std::uint8_t width{unit};
};

/// One layout: what it is for, how big it is, and its keys.
///
/// The keys are ordered row by row and, within a row, left to right.
/// `move()` and `key_at()` both rely on that, and
/// `screen_keyboard_test.cpp` pins it.
struct layout {
  /// The name a host shows and a config file remembers — `prompt`,
  /// `name`, `full`.
  std::string_view name;
  /// The one line a host can put beside the name.
  std::string_view about;
  /// How many rows, and how wide the widest of them is in quarter units.
  /// A host sizes the whole keyboard from the pair.
  std::uint8_t rows{};
  std::uint8_t width{};
  /// The key the focus starts on, named by its make code rather than by
  /// its index, so that inserting a key above it cannot quietly move it.
  /// `default_focus()` is the index.
  std::uint8_t focus_scancode{};
  std::span<const key> keys;
};

/// Every layout, in the order a host should offer them: the smallest
/// first, because the smallest is the one a player wants most often.
[[nodiscard]] std::span<const layout> layouts() noexcept;

/// The layout named `name`, or null for a name this build does not have.
/// Null is a real answer — a host reading a remembered choice out of a
/// config file written by a later build should fall back rather than
/// invent a layout.
[[nodiscard]] const layout* layout_named(std::string_view name) noexcept;

/// Which modifier `scancode` latches, read out of `xt_keyboard::xt_table`
/// and not out of a second list: shift, ctrl and alt latch, and every
/// other key — the three lock keys included, see the header comment —
/// taps.
[[nodiscard]] latch latch_of(std::uint8_t scancode) noexcept;

/// What `key_at()`, `move()` and `default_focus()` answer when there is
/// no key: a point in a gap, or a question asked about a layout that has
/// no such key at all.
inline constexpr std::size_t no_key = static_cast<std::size_t>(-1);

/// The index of `layout::focus_scancode`, or `no_key` for a layout that
/// does not carry that key.
[[nodiscard]] std::size_t default_focus(const layout& which) noexcept;

/// The key covering the point (`row`, `column`) — a row index and a
/// quarter-unit offset from the layout's left edge — or `no_key` for a
/// point in a gap or off the end.
///
/// A host drawing into a framebuffer converts a pixel to a cell (divide
/// by the key size it chose) and asks, rather than deciding for itself
/// what "close enough to a key" means. A host whose keys are widgets has
/// its own toolkit's answer and needs none of this.
[[nodiscard]] std::size_t key_at(const layout& which, std::uint8_t row,
                                 std::uint16_t column) noexcept;

/// A four-way move of the focus.
enum class nav : std::uint8_t { left, right, up, down };

/// Where the focus goes from `focus` when `where` is pushed.
///
/// **Left and right step within the row and wrap at its ends; up and
/// down change row and wrap at the top and the bottom.** Nothing gets
/// stuck against an edge, because a stick held against one is the most
/// ordinary thing a person does with a stick and an edge that swallows
/// it reads as a dead control.
///
/// Up and down land on the key of the next row whose span covers the
/// centre of the key being left — the column, not the index, so that a
/// wide space bar is reached from every key above it, and leaving it
/// again arrives under the finger rather than back at the start of the
/// row. A row with a gap at that column answers with the key whose
/// centre is nearest, ties going left.
///
/// `no_key` in gives `default_focus()` back, so a host that has focused
/// nothing yet can push a direction and get a sensible start.
[[nodiscard]] std::size_t move(const layout& which, std::size_t focus,
                               nav where) noexcept;

/// One key event a commit produces: a make or a break.
struct key_event {
  std::uint8_t scancode{};
  bool down{false};
};

/// What committing a key produces, and what the latch mask becomes.
///
/// At most five events: an ordinary key's make and break, then the
/// breaks of the three modifiers that may have been latched under it.
struct commit {
  std::array<key_event, 5> events{};
  std::size_t count{0};
  /// The mask to carry into the next commit. A host keeps this and
  /// nothing else, and lights the keys whose bit is in it.
  std::uint8_t latched{0};
};

/// Commit key `index` of `which`, with `latched` the mask standing from
/// earlier commits. See the header comment for the contract; the events
/// are in the order a host must post them, and the modifiers' breaks are
/// in ascending scan-code order, which is the order
/// `host::held_keys::release_all()` uses and for the same reason.
///
/// An index that is not a key gives an empty commit and the mask back
/// unchanged.
[[nodiscard]] commit commit_key(const layout& which, std::size_t index,
                                std::uint8_t latched) noexcept;

/// Let go of everything the mask says is down, and nothing else — what a
/// host posts when the keyboard is closed or the window loses focus, so
/// that a latched Shift does not outlive the keyboard that latched it.
[[nodiscard]] commit release_latched(std::uint8_t latched) noexcept;

}  // namespace amberfolio::machine::screen_keyboard
