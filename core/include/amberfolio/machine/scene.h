// SPDX-License-Identifier: AGPL-3.0-only
//
// The scene: where the party is and what is around it, as data (#520).
//
// What a sighted player takes from the 3D view, the automap panel and the
// overland window, for a host that cannot look at a picture: a screen
// reader that wants to say "a door ahead, a wall on the left", an assistant
// that wants to walk without screenshots. `screen_text.h` carries the
// menus; this carries the places, which are not text.
//
// It is a read, like the framebuffer and the screen text: taken between
// runs, out of RAM rather than through the bus, so nothing the program can
// see moves. Nothing is kept, nothing runs per frame, and nothing about it
// is in a hash or a recording.
//
//
// What it says, and what it does not
// ----------------------------------
//
// **Only what the screen shows.** On the interior screen, the faces of
// the squares the 3D view draws — the party's own, and those ahead and
// beside it that sight reaches, by the automap's own sight rule — and the
// faces of every square the automap has revealed, which is what its panel
// draws. A face is open, a wall, an opening (a way through that no rule
// calls a door), a door, or a shut door, by the panel's own door rule
// (`docs/seams.md` §10): a player who hears "door" and a player who sees
// a yellow leaf are given one answer. On the overland, the 5x5 window the
// program paints, each cell with the program's own terrain number and
// whether its move step lets a party onto it.
//
// **The explored map is the automap's**, so it is there only while the
// automap seam is on: with it off nothing records where the party has
// been, and the answer says so (`map_kept` false) rather than calling the
// whole area unexplored.
//
// **In a fight**, the tactical grid's window as the screen draws it: who
// is in it and on which side, who is standing, whose turn it is, and what
// each of its squares costs to step onto. The party's own members are
// listed wherever they are, because a player knows where their own people
// are; an enemy is listed only while it is in the window, because that is
// all the screen shows of it. A figure's hit points and armour class are
// carried where the screen prints them: every party member's, and the
// acting combatant's while it is the one acting.
//
// **Nothing while nothing knows.** No program, a program this build has no
// facts for, or a screen none of the shapes covers — a menu, a shop, the
// camp, a scripted move between areas — and the answer is
// `scene_screen::none`.
//
// The facts are the automap's and the explored overlay's
// (`docs/seams.md` §10, `docs/explored-overlay.md` §2), the travel view's
// move step and the tactical grid's (`docs/hosts.md` §13), each restated in
// `scene.cpp` for its own binary.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {

class machine;
class automap_state;

/// Which of the shapes the scene is.
enum class scene_screen : std::uint8_t {
  /// Nothing is known: see the header comment.
  none,
  /// The interior screen: the 3D view, a sixteen-by-sixteen grid.
  explore,
  /// The overland travel view: a 5x5 window on a 16-by-36 area.
  travel,
  /// A fight: a 7x7 window on a 50-by-25 tactical grid.
  combat,
};

/// Why there is no scene. The first two are the words the screen text and
/// the seams use for the same two situations.
enum class scene_trouble : std::uint8_t {
  none,
  /// No program is known to the machine.
  no_program,
  /// A program this build has no facts for.
  wrong_binary,
  /// The program is not on a screen the scene describes.
  elsewhere,
};

[[nodiscard]] const char* scene_trouble_name(scene_trouble trouble) noexcept;

/// What one border of a square is.
enum class scene_face : std::uint8_t {
  /// Nothing there.
  open,
  /// A solid wall.
  wall,
  /// A way through drawn in the wall that no rule calls a door: an arch.
  opening,
  /// A door the party can walk through.
  door,
  /// A shut door: the program asks before the party goes through.
  shut,
};

[[nodiscard]] const char* scene_face_name(scene_face face) noexcept;

/// The facings, as the program numbers them: the same numbers the map
/// format numbers a square's faces with.
inline constexpr std::uint8_t scene_north = 0;
inline constexpr std::uint8_t scene_east = 2;
inline constexpr std::uint8_t scene_south = 4;
inline constexpr std::uint8_t scene_west = 6;

/// "north", "east", "south" or "west"; "unknown" for anything else.
[[nodiscard]] const char* scene_facing_name(std::uint8_t facing) noexcept;

/// One square of the interior grid: x grows east and y south, both 0..15,
/// and its four faces in the order north, east, south, west.
struct scene_square {
  std::uint8_t x{};
  std::uint8_t y{};
  std::array<scene_face, 4> faces{};

  /// The face on `facing`'s side (one of the four `scene_*` facings).
  [[nodiscard]] scene_face face(std::uint8_t facing) const noexcept {
    return faces[(facing & 6U) / 2U];
  }
};

/// Whether the travel view's move step lets a party onto a cell.
enum class scene_move : std::uint8_t {
  /// It does.
  open,
  /// It never does.
  blocked,
  /// A river: only a party that can cross it; any other is turned back.
  river,
  /// Off the edge of the tactical grid: a step there is a step out of the
  /// fight, and the program asks whether to flee.
  edge,
};

[[nodiscard]] const char* scene_move_name(scene_move move) noexcept;

/// One cell of the overland window.
struct scene_cell {
  /// The program's own number for the cell's terrain: the byte its move
  /// step tests. Two cells with the same number are the same terrain.
  std::uint8_t terrain{};
  scene_move move{scene_move::open};
  /// In a fight, the movement an open square costs; zero elsewhere.
  std::uint8_t cost{};
};

/// Which side of a fight a combatant is on, as the program keeps it: a
/// charmed figure has changed sides.
enum class scene_side : std::uint8_t {
  party,
  enemy,
};

[[nodiscard]] const char* scene_side_name(scene_side side) noexcept;

/// The program's status scale, the word its character sheet prints.
enum class scene_status : std::uint8_t {
  okay,
  animated,
  temporarily_gone,
  running,
  unconscious,
  dying,
  dead,
  stoned,
  gone,
};

[[nodiscard]] const char* scene_status_name(scene_status status) noexcept;

/// The longest name the program gives a figure.
inline constexpr std::size_t scene_name_max = 15;

/// One figure in a fight.
struct scene_combatant {
  std::array<char, scene_name_max> name{};
  std::uint8_t name_length{};
  scene_side side{scene_side::party};
  /// Whether the player gives its orders: false for a monster, a charmed
  /// party member, and a party member handed to the computer (QUICK).
  bool player{false};
  /// Whether it is still in the fight. A party member who is not lies
  /// where it fell; an enemy who is not has left the grid and the list.
  bool standing{false};
  scene_status status{scene_status::okay};
  /// Where it is: the grid's column 0..49 and row 0..24 of its top-left
  /// square, and how many squares it covers each way (a large figure
  /// covers two). `placed` false: the program keeps no square for it.
  bool placed{false};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t width{};
  std::uint8_t height{};
  /// Hit points and armour class, where the screen prints them (see the
  /// header comment); `health_shown` false and both zero elsewhere.
  bool health_shown{false};
  std::uint8_t hp{};
  int ac{};
};

/// How many squares the 3D view can show: at each of three depths the
/// square ahead and the two beside it.
inline constexpr std::size_t scene_view_max = 9;
/// How many squares a grid has.
inline constexpr std::size_t scene_map_max = 256;
/// The overland window's side.
inline constexpr unsigned scene_window_side = 5;
/// The longest zone name, and room to spare.
inline constexpr std::size_t scene_zone_max = 24;
/// The most figures one fight holds: eight party members and the 63
/// monsters the program will add.
inline constexpr std::size_t scene_combatants_max = 71;
/// The tactical window's side, in squares.
inline constexpr unsigned scene_combat_side = 7;
/// The tactical grid's size, in squares.
inline constexpr unsigned scene_combat_columns = 50;
inline constexpr unsigned scene_combat_rows = 25;

struct scene {
  scene_screen screen{scene_screen::none};

  /// Which map: the disk the area's files come from and the area id, as
  /// the program keeps them, and the name the automap prints for it
  /// (`AREA <n>` where it has none). Both screens.
  std::uint8_t disk{};
  std::uint8_t area{};
  std::array<char, scene_zone_max> zone{};
  std::uint8_t zone_length{};

  /// Where the party stands and which way it faces (a `scene_*` facing).
  /// On the interior screen a square of the grid; on the overland a
  /// column 0..15 and a row 0..35 of the area.
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t facing{};

  // --- explore ---------------------------------------------------------

  /// The squares the 3D view draws, nearest first; the first is the
  /// party's own.
  std::array<scene_square, scene_view_max> view{};
  std::uint8_t view_count{};

  /// Whether anything is recording the explored map (the automap seam is
  /// on). False, and `map_count` zero, means "not recorded", not "not
  /// explored".
  bool map_kept{false};
  /// Every square the automap has revealed on this map, row by row.
  std::array<scene_square, scene_map_max> map{};
  std::uint16_t map_count{};

  // --- travel ----------------------------------------------------------

  /// The window's top-left cell in the area's own columns and rows, where
  /// the program puts it. Against an edge of the area's band the window
  /// shows the neighbouring band, so a column can be below 0 or above 15.
  /// In a fight, the tactical window's top-left square on the grid.
  int window_x{};
  int window_y{};
  /// The window, row by row from the top, west to east.
  std::array<scene_cell, std::size_t{scene_window_side} * scene_window_side>
      window{};

  // --- combat ----------------------------------------------------------

  /// The figures, the party's first in the program's own order.
  std::array<scene_combatant, scene_combatants_max> combatants{};
  std::uint8_t combatant_count{};
  /// Whose turn it is, an index into `combatants`; -1 while it is no
  /// one's. `waiting` says the program is waiting for the player's
  /// orders, which is true only on a figure the player commands.
  int turn{-1};
  bool waiting{false};
  /// The window's top-left square on the grid, and its squares, row by
  /// row from the top, west to east (`window_x`/`window_y` hold the
  /// origin).
  std::array<scene_cell, std::size_t{scene_combat_side} * scene_combat_side>
      ground{};
};

/// The scene on `box` now. `out` is `scene_screen::none` unless the answer
/// is `scene_trouble::none`.
[[nodiscard]] scene_trouble read_scene(const machine& box, scene& out) noexcept;

/// The same, from the parts of a machine it reads: conventional memory,
/// where the program's data segment begins in it, and the automap's store
/// when the automap seam is on (null when it is not). No program facts
/// are checked here: the caller has already matched the fingerprint.
[[nodiscard]] scene_trouble read_scene_in(std::span<const std::uint8_t> ram,
                                          std::uint32_t data_base,
                                          const automap_state* kept,
                                          scene& out) noexcept;

/// `scene` as JSON (`docs/hosts.md` §13): `null` when its
/// screen is `none`. Into `out` with a NUL after it when it fits; answers
/// the length without the NUL either way, so a caller asks with an empty
/// span first.
std::size_t scene_json(const scene& from, std::span<char> out) noexcept;

}  // namespace amberfolio::machine
