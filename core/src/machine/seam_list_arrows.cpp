// SPDX-License-Identifier: AGPL-3.0-only
//
// The list-arrows piece of `modern-controls` (seam_modern_controls.cpp): the up
// and down arrows step the highlight in the program's pick-lists, as Home and
// End already do (#423), and step the selected party member at the bars where
// Home and End do (#435).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and scan codes. Not one byte of the
// program is reproduced here.
//
// **There is one vertical pick-list routine**, in overlay 25, and every
// list in the game goes through it: race, gender, class and alignment at
// character creation, the shops, training, coin selection, adding to the
// party, the encounter lists, the camp's Display. It draws the rows,
// keeps a highlight, and reads its keys through the program's menu-bar
// routine, in the same overlay, **in raw mode**.
//
// In raw mode the menu-bar routine hands a key it has no command for
// back as itself, and says so through an out-parameter, a byte in the
// caller's frame whose address the caller passed: one when the key is
// raw, zero when it is a bar command or a confirmation. An extended key
// (an arrow, Home, End, a page key) comes back as its scan code with the
// byte set. The list acts on four of them and ignores the rest:
//
//   * `0x47` (Home) steps up one row, `0x4F` (End) steps down one;
//   * `0x49` (PgUp) and `0x51` (PgDn) page, when a page is there.
//
// Up (`0x48`) and Down (`0x50`) come back the same way and are dropped.
// With the byte *clear*, `0x50` is the bar's own `P`, the Prev command,
// which is why the byte is read before anything is rewritten.
//
// The same routine translates the digits, on the number row and the keypad
// alike: 8 and 2 come back as `0x48` and `0x50` with the byte set, and 7 and
// 1 as `0x47` and `0x4F`. The program cannot tell the number row from the
// keypad (both deliver the same character), so the digits 7 and 1 already
// step a list and, with this seam, 8 and 2 do too.
//
// **The party-member picker** (Trade's receiver, "Cast Spell on whom",
// a script's party pick) lives in the resident image. It reads through
// the same menu-bar routine, in raw mode, and steps on `0x4F` to the
// next member and `0x47` to the previous one, dropping the arrows in the
// same way. Its out-parameter is a byte in its own frame.
//
//
// **The party cursor and the command bars** (#435). Many command bars
// are raw-mode callers of the menu-bar routine too, and hand every raw key
// to one resident routine, the party cursor's, through its thunk. It steps
// the selected member back on `G` (Home) and forward on `O` (End), and **on
// any other key moves the selection to the head of the party**. So at the
// camp bar, its Magic and Alter sub-bars, the post-combat bars, the shops,
// the temples and the script prompts, Up and Down are not dropped: they
// throw away the player's selection. Home and End are the only keys that
// step the member there.
//
// **The party-order screen** is one of them, and Home and End do more
// there: with a member picked up, `G` moves it up the order and `O` moves it
// down. Up and Down do the same with the seam on.
//
// Most of these callers compare the key with letters and not with the
// out-parameter, and a raw scan code is a letter: Up is `H` and Down is `P`.
// Each caller that could act on one was read, and acts on it only with the
// out-parameter clear (docs/seams.md §10 lists each).
//
//
// What the seam does
// ------------------
//
// **Three points.** Two are where the menu-bar routine returns to its
// caller, with AL the key. The handler runs before the instruction that
// stores AL, so a rewrite is what the caller sees:
//
//   * the byte is set and AL is `0x48`: AL becomes `0x47`;
//   * the byte is set and AL is `0x50`: AL becomes `0x4F`;
//   * anything else: nothing is touched.
//
// The third is **inside the menu-bar routine**, at the call into its
// key-read routine (overlay 25, `0x0572`, the point `bar-keys` has too).
// The keystroke it is about to read is the head of the BIOS ring at 40:1Eh.
// If it is an extended Up or Down (character zero, so not a keypad digit),
// and the routine was called from a caller in the table below, the handler
// writes Home or End over it before the program reads it. The program's
// party cursor then steps the member as it does for a typed Home or End.
//
// The program's own steppers do the rest, so the arrows wrap, skip titles
// and scroll exactly as Home and End do.
//
// **The keypad's 8 and 2** (#447). With Num Lock on, the keypad delivers
// the character: ring word `0x4838` and `0x5032`, the arrows' scan codes
// with a digit for a character. The menu-bar routine translates a digit
// in raw mode **after** its read, through the table in the data segment
// (`0x288C`): `1` to `9` become `O P Q K` (a space) `M G H I`, the letters
// the keypad's layout implies, and the out-parameter is set. So `8` comes
// back as `H` and `2` as `P`, which the party cursor does not know, and
// the selection goes to the head. The routine looks at the character and
// never at the scan code, so a rewrite at the same point turns a keypad 8
// into a keypad 7 (`0x4737`, which the table turns into `G`) and a keypad 2
// into a keypad 1 (`0x4F31`, `O`): the cursor steps back and forward, as
// Home and End. The number row's digits (scan codes `0x02` to `0x0B`)
// are not touched: with `hero-keys` they select a member, and without it
// they are the program's own.
//
// **The scope is positive rather than inferred.** The arrows move the
// party in 3D, in the wilderness and in combat, and a host cannot tell
// from outside when one is free. The first two points are reached only from
// inside a pick-list and a picker, so no other screen's arrow is ever
// offered to the handler. **The third is a table of callers**, an
// allowlist: a caller is in it when raw Home and End reach the party cursor
// there and Up and Down do nothing else. A caller the table does not name
// is never touched, and a frame it does not know is not one.
//
// **The callers are identified by the routine's far return address**,
// overlay-qualified, exactly as `bar-keys` does it (seam_menu_bar.h): the
// frame holds the caller's offset and segment, and a caller is in the table
// when its segment is the one the program's overlay manager says that
// module is at now and its offset is the instruction after the call.
//
//   | caller | module | return offset | what Up and Down do there today |
//   |---|---|---|---|
//   | the camp bar | overlay 15 | `0x1F24` | the party cursor: the selection goes to the head |
//   | camp's Magic bar | overlay 15 | `0x1447` | the same |
//   | camp's Alter bar | overlay 15 | `0x1CA4` | the same |
//   | the party-order screen | overlay 15 | `0x17DA` | the same; with a member picked up, nothing |
//   | the post-combat Take bar | overlay 5 | `0x0D91` | nothing: it calls the cursor on `G` and `O` only |
//   | the post-combat treasure bar | overlay 5 | `0x1024` | nothing: the same |
//   | the shop's bar | overlay 6 | `0x061F` | nothing: the same |
//   | the temple's bar | overlay 4 | `0x0DAA` | nothing: the same |
//   | the script prompts | overlay 7 | `0x16EB` | the party cursor: the selection goes to the head |
//
// **Left out, and why**, with the whole audit in docs/seams.md §10: the
// adventuring bars (Up and Down move the party), the combat move loop and
// aim cursor (they move the fighter and the cursor), the stat editor
// (Up and Down are its rows), the rest-time menu (Up and Down are Inc and
// Dec), the game-speed screen (they are its two commands), the main menu
// (its own seam, #434), and every caller that is not raw and does not show
// the party list, which throws an arrow away.
//
// **At a bar that is not raw** (#469). A caller with raw mode off throws
// Up and Down away, so there is no caller to hand a Home or an End to. At
// the door bars, camp's Portraits and Monsters bar and the save slot bar,
// which show the party list, the seam steps the selection itself by the
// party cursor's rule (Up is `G`: the member before, from the head the
// tail; Down is `O`: the member after, the tail stays), writes it to the
// selected-member pointer and calls the program's own roster drawer, and
// answers the key with the one the program throws away. The step and the
// table of those callers are shared with `hero-keys` (seam_party_select.h).
//
// **Checks the byte is a byte the routine writes.** The menu-bar routine
// sets it to zero or one and nothing else. Any other value is not the
// frame these facts describe, and the handler declines and touches
// nothing.
//
// **Qualified as overlay 25.** The list's point is in the overlay and is
// resolved through the program's own word for where it is (seam.h,
// `seam_module::load_segment_at`). The module is identified by its bytes
// as read. The store release's GAME.OVR differs from the repack's only
// inside overlay 2 (docs/seams.md §5), so this module's digest is the
// same on both. The callers' modules are identified by the manager's word
// and the return offset and not by digest: a point in each would make the
// seam inert while that overlay is out of memory, which is most of the time.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On and no arrow pressed at a list or a bar, the run is byte for byte the
// run with the seam off: the handlers read nothing and write nothing unless
// AL, or the ring's head word, is an arrow or the keypad's 8 or 2. The pair
// is an `identical` and a `contrast` (tests/sessions/README.md).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// Nothing outstanding. Out of scope, filed apart: the main menu's Up,
// Down and Enter (#434).

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"
#include "seam_menu_bar.h"
#include "seam_party_select.h"

namespace amberfolio::machine {
namespace {

// --- The module the pick-list lives in -------------------------------------

/// Overlay 25: the menu-bar routine, the vertical pick-list and the Yes/No
/// prompt. The facts are the module's row in the overlay file's own table
/// and the manager's record of the same two numbers in the resident image;
/// the digest is of the bytes as read. The manager's word is found the way
/// `seam_cheats.cpp` documents: search the resident image for the record
/// with the module's file offset and length, one match, and take the word
/// sixteen bytes into the record. The method returns the known words for
/// overlays 8 (`0x360`) and 15 (`0x760`). `bar-keys` has a point in the
/// same module, so the descriptor is shared (seam_menu_bar.h).
constexpr const seam_module& list_module = menu_bar::module;

/// In the pick-list routine: the instruction after its call into the
/// menu-bar routine, which stores the key. Offset from the module's
/// start.
constexpr std::uint32_t list_after_input = 0x0FE0;

/// The out-parameter's address in that routine's frame, below BP.
constexpr std::uint16_t list_flag_below_bp = 0x57;

// --- The party-member picker -----------------------------------------------

/// In the resident image: the instruction after the picker's call into
/// the menu-bar routine, which stores the key. Offset from the image
/// segment; `bar-keys` names the caller by it (seam_menu_bar.h).
constexpr std::uint32_t picker_after_input = menu_bar::picker_return;

/// The out-parameter's address in the picker's frame, below BP.
constexpr std::uint16_t picker_flag_below_bp = 0x2B;

// --- The keys --------------------------------------------------------------

constexpr std::uint8_t scan_home = 0x47;
constexpr std::uint8_t scan_up = 0x48;
constexpr std::uint8_t scan_end = 0x4F;
constexpr std::uint8_t scan_down = 0x50;

/// The same keys as the BIOS ring holds them: scan code high, character
/// low. An extended key's character is zero, which is what tells it from a
/// keypad digit that shares its scan code.
constexpr std::uint16_t ring_home = 0x4700;
constexpr std::uint16_t ring_up = 0x4800;
constexpr std::uint16_t ring_end = 0x4F00;
constexpr std::uint16_t ring_down = 0x5000;

/// The keypad's digits with Num Lock on: the scan code is the one the arrow
/// or navigation key has, and the character is the digit, which is what
/// tells them from the extended keys. 8 and 2 are what the seam reads; 7 and
/// 1 are what it writes.
constexpr std::uint16_t ring_pad_8 = 0x4838;
constexpr std::uint16_t ring_pad_2 = 0x5032;
constexpr std::uint16_t ring_pad_7 = 0x4737;
constexpr std::uint16_t ring_pad_1 = 0x4F31;

/// The menu-bar routine has just returned AL, with its out-parameter at
/// SS:BP-`flag_below_bp`. If the byte says the key is a raw one and the
/// key is an arrow, hand back Home or End instead.
void step_like_home_and_end(machine& box, seam_context& ctx,
                            std::uint16_t flag_below_bp) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  const std::uint8_t key = regs.get(cpu::reg8::al);
  if (key != scan_up && key != scan_down) {
    return;
  }

  const std::uint8_t raw = cpu.read_byte(
      regs[cpu::sreg::ss],
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - flag_below_bp));
  if (raw == 0) {
    // A bar command, not a raw key: 0x50 here is the bar's own Prev.
    return;
  }
  if (raw != 1) {
    // The routine writes zero or one. Not the frame these facts describe.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  regs.set(cpu::reg8::al, key == scan_up ? scan_home : scan_end);
}

void step_the_list(machine& box, seam_context& ctx) {
  step_like_home_and_end(box, ctx, list_flag_below_bp);
}

void step_the_picker(machine& box, seam_context& ctx) {
  step_like_home_and_end(box, ctx, picker_flag_below_bp);
}

// --- The command bars that step the party cursor ---------------------------

using menu_bar::caller;

/// The callers whose Home and End step the selected party member and whose
/// Up and Down do nothing else (docs/seams.md §10 has each by two routes).
/// Offsets are the instruction after the call into the menu-bar routine; the
/// words are the overlay manager's, from seam_menu_bar.h.
constexpr std::array<caller, 9> roster_callers{{
    {.load_segment_at = menu_bar::camp_load_segment_at,
     .return_offset = 0x1F24},  // the camp bar
    {.load_segment_at = menu_bar::camp_load_segment_at,
     .return_offset = 0x1447},  // camp's Magic bar
    {.load_segment_at = menu_bar::camp_load_segment_at,
     .return_offset = 0x1CA4},  // camp's Alter bar
    {.load_segment_at = menu_bar::camp_load_segment_at,
     .return_offset = 0x17DA},  // the party-order screen
    {.load_segment_at = menu_bar::post_combat_load_segment_at,
     .return_offset = 0x0D91},  // the post-combat Take bar
    {.load_segment_at = menu_bar::post_combat_load_segment_at,
     .return_offset = 0x1024},  // the post-combat treasure bar
    {.load_segment_at = menu_bar::shop_load_segment_at,
     .return_offset = 0x061F},  // the shop's bar
    {.load_segment_at = menu_bar::temple_load_segment_at,
     .return_offset = 0x0DAA},  // the temple's bar
    {.load_segment_at = menu_bar::script_load_segment_at,
     .return_offset = 0x16EB},  // the script prompts
}};

/// What each key the handler acts on becomes. The arrows become Home and End.
/// The keypad's 8 and 2 with Num Lock on (a character, so not the arrows)
/// become its 7 and 1, which the menu-bar routine's own table turns into
/// `G` and `O`; the scan code is Home's and End's, so the keys are told
/// apart from the number row's, whose scan codes are `0x02` to `0x0B`.
struct rewrite {
  std::uint16_t from;
  std::uint16_t to;
};

constexpr std::array<rewrite, 4> rewrites{
    {{.from = ring_up, .to = ring_home},
     {.from = ring_down, .to = ring_end},
     {.from = ring_pad_8, .to = ring_pad_7},
     {.from = ring_pad_2, .to = ring_pad_1}}};

/// Up and Down at a bar that is **not raw** (#469), where the menu-bar
/// routine throws every extended key away and there is no caller to hand a
/// Home or an End to. The target is what the party cursor would have
/// chosen (`step::back` for Up, `step::forward` for Down), and the step is
/// the one `hero-keys` takes for a digit there (seam_party_select.h):
/// the selection written, the program's own roster drawer called, the key
/// answered with the one the program throws away.
void select_at_a_bar_that_is_not_raw(machine& box, seam_context& ctx,
                                     const menu_bar::pending_key& pending) {
  if (pending.key != ring_up && pending.key != ring_down) {
    return;
  }
  cpu::processor& cpu = box.processor();
  switch (party_select::at_a_party_bar(cpu, ctx, 0)) {
    case party_select::where::not_here:
      return;
    case party_select::where::frame_unknown:
      ctx.decline(seam_reason::point_not_recognized);
      return;
    case party_select::where::here:
      break;
  }
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  if (!party_select::is_the_data_segment(ctx, ds)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const party_select::party members = party_select::read_party(cpu, ds);
  if (!members.readable) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (party_select::map_over_the_roster(box)) {
    return;
  }

  const unsigned current = party_select::member_index(
      members, party_select::read_pointer(cpu, ds, party_select::data_current));
  const unsigned target = party_select::stepped_index(
      members, current,
      pending.key == ring_up ? party_select::step::back
                             : party_select::step::forward);
  if (target >= members.size || target == current) {
    // Nobody to step to (the tail stays, a party of one, a selection that
    // is no member): the routine goes back to waiting, as it would have
    // for a key it threw away.
    party_select::answer_with_the_ignored_key(cpu, pending.at);
    return;
  }
  if (!party_select::select_and_redraw(box, ctx, ds, members.member[target],
                                       pending.at)) {
    ctx.decline(seam_reason::point_not_recognized);
  }
}

/// The program is about to read the keystroke at the head of the ring. If it
/// is an extended Up or Down, or the keypad's 8 or 2, and the caller is one
/// that steps the party cursor on Home and End, make it Home or End (or the
/// keypad's 7 or 1).
void step_the_party(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();

  const std::array<std::uint16_t, 4> wanted{ring_up, ring_down, ring_pad_8,
                                            ring_pad_2};
  const std::optional<menu_bar::pending_key> pending =
      menu_bar::key_about_to_be_read(box, wanted);
  if (!pending) {
    return;
  }
  if (!menu_bar::called_from(cpu, ctx, roster_callers)) {
    select_at_a_bar_that_is_not_raw(box, ctx, *pending);
    return;
  }
  for (const rewrite& r : rewrites) {
    if (r.from == pending->key) {
      cpu.write_word(bda::segment, pending->at, r.to);
      return;
    }
  }
}

constexpr std::array<seam_point, 3> list_arrows_point_table{
    {{.module = list_module, .offset = list_after_input, .run = &step_the_list},
     {.module = resident_image,
      .offset = picker_after_input,
      .run = &step_the_picker},
     {.module = list_module,
      .offset = menu_bar::key_read_call,
      .run = &step_the_party}}};
static_assert(list_arrows_point_table.size() == list_arrows_point_count);

}  // namespace

std::span<const seam_point> list_arrows_points() noexcept {
  return list_arrows_point_table;
}

}  // namespace amberfolio::machine
