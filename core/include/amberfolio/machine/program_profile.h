// SPDX-License-Identifier: AGPL-3.0-only
//
// The program profile: one table per program image of the facts that more
// than one seam, or a seam and a reader, act on (PLAN.md §7, M9; #552).
//
// A fact about a program is an address, an offset, a size or a value the
// program gives a byte (CONTRIBUTING.md). Most of them belong to the one
// seam that acts on them, and stay in that seam's own table. The ones
// here are the ones that would otherwise be written down in several
// files: the game mode byte, the party's records, the bar highlight, the
// routines a seam watches or calls, the overlay modules and the words the
// program keeps their load segments in. Each is stated once, here, for
// each program that has it, and every file reads it from here.
//
// **A profile is about one program image**, named by its SHA-256 the way
// a seam's fingerprints are (seam.h). A seam is available on the programs
// whose facts it has, which is PLAN.md §5's per-binary rule: a file that
// acts on these facts names the profiles it was checked against, and its
// `fingerprints` are theirs. A program without a profile has no seam.
//
// **Two programs have a profile**: the baseline edition (edition.h), in
// full, and Curse of the Azure Bonds, in the part its seams so far read.
// A second program is a second instance of this struct, filled in from
// its own image, with every fact checked twice and confirmed by a driven
// run; nothing here is ever assumed to carry over.
//
// The members are named for what the program does with a fact, not for
// where it is, so that a second program's profile answers the same
// question with its own address. The doc comment on each says what the
// fact means; where it was found and how it was checked is in
// `docs/seams.md` §10 and the file that first needed it.
//
// All of it addresses and offsets. Not one byte of a program is
// reproduced here.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/overlay.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

struct program_profile {
  /// For a person reading a listing: the game, nominatively (TRADEMARK.md).
  std::string_view name;

  /// The SHA-256 of the program image every fact below is about, as 64
  /// lowercase hex characters.
  std::string_view fingerprint;

  // --- The image -----------------------------------------------------------

  /// Where the data segment begins, as paragraphs from the image base.
  /// Every `data_` offset below is in that segment, which is the one DS
  /// addresses throughout the program. A seam reads DS at its points and
  /// checks it against this rather than trusting it: the two-route rule,
  /// applied to the one fact every other is relative to.
  std::uint16_t dgroup_paragraphs;

  /// The data segment's start as a byte offset from the image base.
  [[nodiscard]] constexpr std::uint32_t dgroup_offset() const noexcept {
    return std::uint32_t{dgroup_paragraphs} << 4U;
  }

  /// The paragraph the roster drawer and the screen composer were linked
  /// at. **A paragraph and an offset, not a flat image offset**: both
  /// reach their own literals as `CS:<constant>` and their siblings as
  /// `push cs` plus a near call, so they work only when CS is the segment
  /// they were linked at. Called as a flat offset from the image base, the
  /// same bytes run and read their literals out of the wrong memory
  /// (docs/seams.md §8.4).
  std::uint16_t screen_paragraph;
  /// The roster drawer, in that paragraph: far, one argument (a far
  /// pointer to the current party member, segment first), and it cleans
  /// its own four bytes. All it does with the pointer is decide which row
  /// is drawn white; the list itself it walks from the party's head.
  std::uint16_t roster_draw_offset;
  /// The screen composer, in that paragraph: the routine the program calls
  /// on the way out of every full-screen view. It draws the scaffold (the
  /// outer frame, the bottom panel, the viewport box and its inset) and
  /// then, for whichever mode the program is in, the view, the party
  /// roster and the status line. It takes nothing and cleans nothing.
  std::uint16_t screen_composer_offset;

  // --- Resident routines, as offsets from the image base -------------------

  /// The keyboard: "is a key waiting" (the program's poll, which is where
  /// it is between commands) and the blocking read (`seam_key_read.h`).
  std::uint16_t key_pending_entry;
  std::uint16_t key_read_entry;

  /// The region clear and the whole-screen clear.
  std::uint16_t clear_region_entry;
  std::uint16_t clear_screen_entry;

  /// The framed box, and the string drawer: the two routines a seam's
  /// own box is made of (#188, `docs/seams.md` §3).
  std::uint16_t draw_frame_entry;
  std::uint16_t draw_string_entry;

  /// The resident thunk of the program's **one** menu-bar input routine
  /// (M5-E2d): every bar the game puts on the screen and every key it
  /// reads through one goes past here, with the bar itself as an argument.
  /// A far jump into the overlay that holds the routine, so at its entry
  /// the stack is still the caller's frame. The adventuring screen's own
  /// bar is a pointer into the data segment and everybody else's is a
  /// string built on the stack, which is how a seam knows whose screen it
  /// is on.
  std::uint16_t command_bar_entry;

  /// The far return of the roster drawer: its `retf`, not its entry. The
  /// drawer clears each of its own rows through the box-region clear, so
  /// at its entry the roster is about to go; at the return the list is on
  /// the screen and every clear it made is behind it.
  std::uint16_t roster_drawn_return;

  // --- The overlays ----------------------------------------------------------
  //
  // A module is the read that loads it (file, offset, length, digest) and
  // the word in the resident image the overlay manager keeps its load
  // segment in (overlay.h, `seam_module::load_segment_at`, #131). The
  // read's facts say *which* module it is, and are what a person checks
  // against the overlay file's own table; the word says *where it is now*,
  // which the read cannot, because the manager moves a module inside its
  // arena without going near DOS.
  //
  // **How a word is found and checked.** The word sits inside the
  // manager's own record for the module, which begins sixteen bytes
  // earlier and carries, in order, the module's file offset and length.
  // Search the resident image for the record with the module's two
  // numbers, one match, and take the word sixteen bytes past its start.
  // The search returns every word below that a driven run has confirmed,
  // which is its second route.

  /// The title screens'.
  seam_module titles_overlay;
  /// The event-script interpreter's.
  seam_module interpreter_overlay;
  /// Tactical combat's: the round loop and the end check.
  seam_module combat_overlay;
  /// A fight's aim: the target bar and the manual aim.
  seam_module aim_overlay;
  /// The adventuring screen's loop and its two command bars.
  seam_module adventure_overlay;
  /// The camp screen and the rest command.
  seam_module camp_overlay;
  /// The main menu's loop and the party roster's.
  seam_module roster_overlay;
  /// The saved-game slots and the roster's character list.
  seam_module slots_overlay;
  /// The character view and its sheet.
  seam_module view_overlay;
  /// The menu-bar routine and the pick-list routine.
  seam_module menu_overlay;

  /// The load-segment words of modules no seam has a point in, which a
  /// seam names only as the module a call came from (`seam_menu_bar.h`).
  std::uint16_t temple_load_segment_at;
  std::uint16_t post_combat_load_segment_at;
  std::uint16_t shop_load_segment_at;
  std::uint16_t script_runner_load_segment_at;
  std::uint16_t rest_load_segment_at;
  std::uint16_t appraise_load_segment_at;
  std::uint16_t death_load_segment_at;

  // --- In the menu overlay ---------------------------------------------------

  /// In the menu-bar routine: the call into the key-read routine, reached
  /// only when the key-pending routine has said a key is waiting.
  std::uint16_t menu_key_read_call;
  /// Below the routine's BP: the bar it was handed, copied as a Pascal
  /// string whose characters are numbered from one, and that copy's bound.
  std::uint16_t menu_local_bar;
  std::uint8_t menu_bar_capacity;
  /// In the pick-list routine: the instruction after its call into the
  /// menu-bar routine.
  std::uint16_t pick_list_bar_return;

  // --- In the adventure overlay ----------------------------------------------

  /// The loop's two calls into the menu-bar routine, with the overhead
  /// view's bar and with the 3D view's: each call, and the instruction
  /// after it, which stores what the routine answered. A point there runs
  /// before that store, so the letter is still in AL.
  std::uint16_t area_bar_call;
  std::uint16_t area_bar_return;
  std::uint16_t view_bar_call;
  std::uint16_t view_bar_return;

  // --- The data segment ------------------------------------------------------

  /// The game mode byte, and the values a seam or a reader tells apart:
  /// the camp screen, the travel view (the wilderness, and an area drawn
  /// flat), the adventuring screen, and tactical combat.
  std::uint16_t data_game_mode;
  std::uint8_t mode_camp;
  std::uint8_t mode_travel;
  std::uint8_t mode_adventure;
  std::uint8_t mode_combat;

  /// Which view is up: the sixteen-by-sixteen interior grid, or one of the
  /// wilderness areas, which are numbered from the first to the last.
  std::uint16_t data_view_kind;
  std::uint8_t view_kind_grid;
  std::uint8_t view_kind_first_overland;
  std::uint8_t view_kind_last_overland;

  /// Non-zero while the program is running a scripted move between areas;
  /// the position is not to be believed while it is.
  std::uint16_t data_in_transition;

  /// The disk the area's files come from, and the area's own id.
  std::uint16_t data_disk_number;
  std::uint16_t data_area_id;

  /// The party's square and facing everywhere but the overland; facing 0
  /// is north, 2 east, 4 south and 6 west.
  std::uint16_t data_party_x;
  std::uint16_t data_party_y;
  std::uint16_t data_party_facing;

  /// The wall-set descriptor: which WALLDEF block each of the three slots
  /// holds, a word per slot at a stride of four from here, slot 1 at `+4`
  /// (`automap_faces.h`).
  std::uint16_t data_wall_set;

  /// The loaded map: a far pointer, offset then segment. A zero segment is
  /// "no map", which is the state every screen that is not the adventure
  /// screen leaves it in.
  std::uint16_t data_map_pointer;

  /// The current area record, as a far pointer; inside it, the party's
  /// overland column and row (words), the loaded geometry block (the
  /// third byte of a map's identity), and the word that says the program
  /// draws the wilderness in 3D instead.
  std::uint16_t data_area_record;
  std::uint16_t area_overland_column;
  std::uint16_t area_overland_row;
  std::uint16_t area_geometry_block;
  std::uint16_t area_shown_in_3d;

  /// The adventuring screen's own two command bars, as Pascal strings the
  /// program keeps: the overhead view's and the 3D view's. Every other
  /// bar in the game is a copy built on the stack (M5-E2d).
  std::uint16_t data_area_bar;
  std::uint16_t data_view_bar;

  /// The selected party member, and the party's head: far pointers,
  /// offset then segment.
  std::uint16_t data_current_member;
  std::uint16_t data_roster_head;

  /// The menu bar's highlight: **one byte that every bar in the program
  /// shares** (M5-E1g, #304). The menu-bar routine reads it on entry as a
  /// one-based group index and resets it to the first group when it names
  /// a group the bar it was handed does not have; the two cursor keys step
  /// it, with a wrap at either end; and choosing a command sets it to that
  /// command's group. A group is one command letter (the routine's class
  /// is `0-9A-Z`, upper case only, #221) and the characters after it up to
  /// the next, which is why one capital per word makes one group per word.
  std::uint16_t data_bar_highlight;

  /// The program's one-byte pushback slot for an extended key's second
  /// half. While it is armed the head of the BIOS ring is not the key
  /// about to be read.
  std::uint16_t data_key_pushback;

  /// The far pointer to the 8x8 font the program draws every menu and
  /// message with. Both words zero is "the font has not been loaded yet".
  std::uint16_t data_font_pointer;

  /// The overland terrain table's per-view-kind column bias, a byte table.
  std::uint16_t data_view_column_bias;

  /// The sound driver's busy word: set, the driver hooks the timer for
  /// each sound and unhooks it after its wait (`seam_timer_fix.cpp`).
  std::uint16_t data_sound_busy;

  // --- A character record ----------------------------------------------------

  /// Offsets into a record: the next record's far pointer, the wound
  /// status, the held byte (whether the record is in its side's body
  /// count, and what the program's own screens gate acting on), the
  /// combat side (signed, 0 the party's), and the current hit points, a
  /// byte.
  std::uint16_t record_next;
  std::uint16_t record_status;
  std::uint16_t record_held;
  std::uint16_t record_side;
  std::uint16_t record_hit_points;
};

/// Pool of Radiance's overlay modules, which its profile below names.
namespace pool_of_radiance_modules {

/// Overlay 1.
inline constexpr seam_module titles{
    .file = "GAME.OVR",
    .file_offset = 212,
    .length = 2062,
    .digest =
        "b6ca55979d60ca53a0e0063c4929a61ca8c5f21843f77fd390598ee29fc4967a",
    .load_segment_at = 0x1A0};

/// Overlay 3.
inline constexpr seam_module interpreter{
    .file = "GAME.OVR",
    .file_offset = 3398,
    .length = 13581,
    .digest =
        "56c6135af78c125823c0d8066fcfeefc7cfd1121d752ca398fed5c37fa17182a",
    .load_segment_at = 0x200};

/// Overlay 8.
inline constexpr seam_module combat{
    .file = "GAME.OVR",
    .file_offset = 38919,
    .length = 4735,
    .digest =
        "5d07a6b3fedb56509214f24bdbdbc3b8625ddf6b2ce4d6274e6e89b26c563930",
    .load_segment_at = 0x360};

/// Overlay 13.
inline constexpr seam_module aim{
    .file = "GAME.OVR",
    .file_offset = 75771,
    .length = 15474,
    .digest =
        "19eb3541795a718bf02cc1db08ef2802c286b620c440269f36793e0829464578",
    .load_segment_at = 0x690};

/// Overlay 14.
inline constexpr seam_module adventure{
    .file = "GAME.OVR",
    .file_offset = 91851,
    .length = 4268,
    .digest =
        "ce2018e8e9d51d422d12e2a8e60837322af70639bcbca2b9fe34ccfd333e2d3a",
    .load_segment_at = 0x730};

/// Overlay 15.
inline constexpr seam_module camp{
    .file = "GAME.OVR",
    .file_offset = 96305,
    .length = 8158,
    .digest =
        "9b9153998d07b8e16d2466d95670cdd683e4dc587281e162d05d15c3458d362b",
    .load_segment_at = 0x760};

/// Overlay 16.
inline constexpr seam_module roster{
    .file = "GAME.OVR",
    .file_offset = 104831,
    .length = 17497,
    .digest =
        "232c6aa60fda1a475d7933aabc97b598fba93d8e4af4026e3551bb0ef06045a0",
    .load_segment_at = 0x790};

/// Overlay 17.
inline constexpr seam_module slots{
    .file = "GAME.OVR",
    .file_offset = 122854,
    .length = 9189,
    .digest =
        "30da2d2a28149942431395bf59511b55a9eab4d8f728bff625c5a22d81638882",
    .load_segment_at = 0x7D0};

/// Overlay 19.
inline constexpr seam_module view{
    .file = "GAME.OVR",
    .file_offset = 135226,
    .length = 11026,
    .digest =
        "3011cde3c5b83ac154a7c9f735c4c95d5449154cf250a655992c1fcbb04911e1",
    .load_segment_at = 0x860};

/// Overlay 25.
inline constexpr seam_module menu{
    .file = "GAME.OVR",
    .file_offset = 182479,
    .length = 4682,
    .digest =
        "175454bc2f527dd6757c89eaa50a6cdd27a9cf5d3aaa197b33a140f5b09a3901",
    .load_segment_at = 0x3C60};

}  // namespace pool_of_radiance_modules

/// The baseline edition (edition.h): the program image every fact in this
/// build's seam tables was gathered on.
///
/// Second routes, where `docs/seams.md` §10 does not carry them: the two
/// keyboard entries by a driven run's own stop line, which names the far
/// address of the instruction after the read's INT 16h; the box-region
/// clear by two seams that found it independently, with the frame border
/// beside it at `0x3F10` putting the segment where both agree; the party's
/// square and facing by `docs/playable.md`'s watch list; the bar highlight
/// by watching the byte through a driven camp; and the load-segment words
/// by the search above, which returns the combat overlay's `0x360` that a
/// driven fight confirmed and the camp's `0x760` that a driven camp did.
inline constexpr program_profile pool_of_radiance{
    .name = "Pool of Radiance",
    .fingerprint =
        "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",

    .dgroup_paragraphs = 0xC7C,
    .screen_paragraph = 0x0BA,
    .roster_draw_offset = 0x0767,
    .screen_composer_offset = 0x27D9,

    .key_pending_entry = 0xA6FD,
    .key_read_entry = 0xA70F,
    .clear_region_entry = 0x4047,
    .clear_screen_entry = 0x7D3B,
    .draw_frame_entry = 0x41F8,
    .draw_string_entry = 0x76B6,
    .command_bar_entry = 0x3C7A,
    .roster_drawn_return = 0x148A,

    .titles_overlay = pool_of_radiance_modules::titles,
    .interpreter_overlay = pool_of_radiance_modules::interpreter,
    .combat_overlay = pool_of_radiance_modules::combat,
    .aim_overlay = pool_of_radiance_modules::aim,
    .adventure_overlay = pool_of_radiance_modules::adventure,
    .camp_overlay = pool_of_radiance_modules::camp,
    .roster_overlay = pool_of_radiance_modules::roster,
    .slots_overlay = pool_of_radiance_modules::slots,
    .view_overlay = pool_of_radiance_modules::view,
    .menu_overlay = pool_of_radiance_modules::menu,

    .temple_load_segment_at = 0x230,         // overlay 4
    .post_combat_load_segment_at = 0x260,    // overlay 5
    .shop_load_segment_at = 0x290,           // overlay 6
    .script_runner_load_segment_at = 0x2C0,  // overlay 7
    .rest_load_segment_at = 0x8D0,           // overlay 20
    .appraise_load_segment_at = 0x900,       // overlay 21
    .death_load_segment_at = 0x3E10,         // overlay 31

    .menu_key_read_call = 0x0572,
    .menu_local_bar = 0x53,
    .menu_bar_capacity = 40,
    .pick_list_bar_return = 0x0FE0,

    .area_bar_call = 0x09D0,
    .area_bar_return = 0x09D5,
    .view_bar_call = 0x0C40,
    .view_bar_return = 0x0C45,

    .data_game_mode = 0x49F3,
    .mode_camp = 2,
    .mode_travel = 3,
    .mode_adventure = 4,
    .mode_combat = 5,

    .data_view_kind = 0x49FA,
    .view_kind_grid = 1,
    .view_kind_first_overland = 2,
    .view_kind_last_overland = 4,

    .data_in_transition = 0x442F,
    .data_disk_number = 0x5376,
    .data_area_id = 0x84DC,

    .data_party_x = 0x6AAD,
    .data_party_y = 0x6AAE,
    .data_party_facing = 0x6AAF,
    .data_wall_set = 0x6AAE,
    .data_map_pointer = 0x6A5C,

    .data_area_record = 0x49D2,
    .area_overland_column = 0x186,
    .area_overland_row = 0x188,
    .area_geometry_block = 0x18A,
    .area_shown_in_3d = 0x1CC,

    .data_area_bar = 0x04B6,
    .data_view_bar = 0x04DF,

    .data_current_member = 0x5D92,
    .data_roster_head = 0x5D96,
    .data_bar_highlight = 0x6B2B,
    .data_key_pushback = 0x8501,
    .data_font_pointer = 0x5E20,
    .data_view_column_bias = 0x3C76,
    .data_sound_busy = 0x5374,

    .record_next = 0x104,
    .record_status = 0x10C,
    .record_held = 0x10D,
    .record_side = 0x10E,
    .record_hit_points = 0x11B,
};

/// Curse of the Azure Bonds' overlay modules, which its profile below
/// names. Numbered, like Pool of Radiance's, by their place in the
/// program's own stub table.
namespace curse_of_the_azure_bonds_modules {

/// Overlay 3: the event-script interpreter, which carries a script's own
/// protection command (`seam_code_wheel.cpp`). The stub's record is at
/// image `0x03C0`.
inline constexpr seam_module interpreter{
    .file = "GAME.OVR",
    .file_offset = 2313,
    .length = 14858,
    .digest =
        "323a088764862961e792f70af6091e8d72dddd748c0f46a78a9469000a80888c",
    .load_segment_at = 0x3D0};

/// Overlay 9: tactical combat's round loop and its end check. The stub's
/// record is at image `0x0570`: the module's file offset and length, and
/// the load-segment word sixteen bytes in.
inline constexpr seam_module combat{
    .file = "GAME.OVR",
    .file_offset = 42328,
    .length = 4995,
    .digest =
        "312497d5638d2a35c91d4b0878ff00b73e5b9220249e866378b83e4125e55110",
    .load_segment_at = 0x580};

/// Overlay 14: a fight's aim, the target bar and the manual aim. The
/// stub's record is at image `0x08D0`.
inline constexpr seam_module aim{
    .file = "GAME.OVR",
    .file_offset = 78696,
    .length = 18872,
    .digest =
        "0b4a75f00ede803ba4c79d199b57fe26cbc8fa7f5c50c956a367203387cce874",
    .load_segment_at = 0x8E0};

/// Overlay 15: the adventuring screen's loop and its command bar. The
/// stub's record is at image `0x0990`.
inline constexpr seam_module adventure{
    .file = "GAME.OVR",
    .file_offset = 98348,
    .length = 3724,
    .digest =
        "680af9ef042b5c0080bbee04a98ae7ff92572b36f3fa0083654e9bbfcb52d22f",
    .load_segment_at = 0x9A0};

/// Overlay 16: the camp screen. The stub's record is at image `0x09D0`.
inline constexpr seam_module camp{
    .file = "GAME.OVR",
    .file_offset = 102254,
    .length = 9749,
    .digest =
        "8dfb1afafc265996e3978a03582804dac10cdbae075f2b1300c6e3ba6af8ef79",
    .load_segment_at = 0x9E0};

/// Overlay 18: the main menu's loop and the character editor. The stub's
/// record is at image `0x0A70`.
inline constexpr seam_module roster{
    .file = "GAME.OVR",
    .file_offset = 131047,
    .length = 22202,
    .digest =
        "5cc199bf1e98c46dec6df1423f6037650de72e5334f1dedac4dae38c8d6a6373",
    .load_segment_at = 0xA80};

/// Overlay 27: the menu-bar routine, the pick-list routine and the Yes/No
/// prompt. The stub's record is at image `0x1040`.
inline constexpr seam_module menu{
    .file = "GAME.OVR",
    .file_offset = 245901,
    .length = 4957,
    .digest =
        "2447cf5dd78b6ff16bf9ad6aecef8504dc934d2a39624d0e6350333842351d57",
    .load_segment_at = 0x1050};

}  // namespace curse_of_the_azure_bonds_modules

/// Curse of the Azure Bonds, the program Steam and GOG sell (v1.3;
/// edition.h), **in part** (#554, #555). Only the members the seams and
/// reads ported to it use are filled in, each checked against the
/// program's own image and by a driven run (`docs/seams.md` §10,
/// `docs/hosts.md` §13). Every other member is zero, which is not a fact
/// about this program but the absence of one, and nothing available on
/// this program reads it.
///
/// **No view kind.** The program has one view in the adventuring mode,
/// the grid, and its mode 3 is the overland journey, a list of places
/// rather than a map walked square by square, so `data_view_kind` is zero
/// and a read takes the grid as given.
///
/// **One adventuring bar.** The overhead view is toggled inside the same
/// loop as the 3D view and keeps its bar, so the 3D view's bar and its
/// call's return are filled and the overhead view's are zero (M5-E2d,
/// `seam_automap.cpp`). Its roster drawer is overlaid, so the profile's
/// resident roster members are zero too and the automap names the stub.
///
/// The record is the 422-byte one the program also saves, so its offsets
/// are the save file's: the same fields as Pool of Radiance's in the
/// same order, further in.
inline constexpr program_profile curse_of_the_azure_bonds{
    .name = "Curse of the Azure Bonds",
    .fingerprint =
        "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd",
    .dgroup_paragraphs = 0xABE,
    .screen_paragraph = {},
    .roster_draw_offset = {},
    .screen_composer_offset = {},
    .key_pending_entry = 0x86EB,
    .key_read_entry = 0x86FD,
    .clear_region_entry = 0x17FD,
    .clear_screen_entry = 0x56E2,
    .draw_frame_entry = {},
    .draw_string_entry = {},
    .command_bar_entry = 0x106A,
    .roster_drawn_return = {},
    .titles_overlay = {},
    .interpreter_overlay = curse_of_the_azure_bonds_modules::interpreter,
    .combat_overlay = curse_of_the_azure_bonds_modules::combat,
    .aim_overlay = curse_of_the_azure_bonds_modules::aim,
    .adventure_overlay = curse_of_the_azure_bonds_modules::adventure,
    .camp_overlay = curse_of_the_azure_bonds_modules::camp,
    .roster_overlay = curse_of_the_azure_bonds_modules::roster,
    .slots_overlay = {},
    .view_overlay = {},
    .menu_overlay = curse_of_the_azure_bonds_modules::menu,
    .temple_load_segment_at = 0x430,         // overlay 5
    .post_combat_load_segment_at = 0x460,    // overlay 6
    .shop_load_segment_at = 0x490,           // overlay 7
    .script_runner_load_segment_at = 0x4D0,  // overlay 8
    .rest_load_segment_at = {},
    .appraise_load_segment_at = {},
    .death_load_segment_at = 0x1220,  // overlay 33
    .menu_key_read_call = 0x05F1,
    .menu_local_bar = 0x53,
    .menu_bar_capacity = 40,
    .pick_list_bar_return = 0x10F3,
    .area_bar_call = {},
    .area_bar_return = {},
    .view_bar_call = {},
    .view_bar_return = 0x092C,
    .data_game_mode = 0x4FD3,
    .mode_camp = 2,
    .mode_travel = 3,
    .mode_adventure = 4,
    .mode_combat = 5,
    .data_view_kind = {},
    .view_kind_grid = {},
    .view_kind_first_overland = {},
    .view_kind_last_overland = {},
    .data_in_transition = 0x47F9,
    .data_disk_number = 0x5C08,
    .data_area_id = 0x8B78,
    .data_party_x = 0x7229,
    .data_party_y = 0x722A,
    .data_party_facing = 0x722B,
    .data_wall_set = 0x722A,
    .data_map_pointer = 0x7220,
    .data_area_record = 0x4FB2,
    .area_overland_column = 0x186,
    .area_overland_row = 0x188,
    .area_geometry_block = 0x18A,
    .area_shown_in_3d = 0x1CC,
    .data_area_bar = {},
    .data_view_bar = 0x03D8,
    .data_current_member = 0x6520,
    .data_roster_head = 0x6524,
    .data_bar_highlight = 0x72AE,
    .data_key_pushback = 0x8CBF,
    .data_font_pointer = 0x65AE,
    .data_view_column_bias = {},
    .data_sound_busy = {},
    .record_next = 0x189,
    .record_status = 0x195,
    .record_held = 0x196,
    .record_side = 0x197,
    .record_hit_points = 0x1A4,
};

/// Every program with a profile.
[[nodiscard]] std::span<const program_profile* const>
program_profiles() noexcept;

/// The profile of the program image whose SHA-256 is `digest`, or null:
/// a program this build has no facts about.
[[nodiscard]] const program_profile* find_program_profile(
    const sha256_digest& digest) noexcept;

/// The fingerprints of the programs in `programs`, for a seam's
/// `fingerprints` (seam.h): a seam is available on the programs whose
/// facts it has, and on no other.
template <std::size_t N>
[[nodiscard]] constexpr std::array<std::string_view, N> fingerprints_of(
    const std::array<const program_profile*, N>& programs) noexcept {
  std::array<std::string_view, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = programs[i]->fingerprint;
  }
  return out;
}

}  // namespace amberfolio::machine
