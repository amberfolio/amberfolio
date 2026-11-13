// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card's table (#427). key_card.h says what it is and what is
// not on it; this file is the data and the two readings of it.
//
// **To add a seam's keys, add a line in the context where the key is
// pressed** and name the seam as the third word. The row shows only while
// that seam is on, and the unit suite checks the name against
// `all_seams()`. A row that is true of one program names it as the fourth
// word, and the suite checks that against the edition table.
//
// How each row was checked is in docs/hosts.md: driven against the
// running program where that is cheap, and against the routine that
// reads the key where it is not.

#include "amberfolio/host/key_card.h"

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/machine/edition.h"

namespace amberfolio::host {
namespace {

// The words a row or a context names a program by: what
// `key_card_program()` answers for the two editions this build knows. The
// unit suite holds each to the edition table.
constexpr std::string_view pool = "pool";
constexpr std::string_view curse = "curse";
constexpr std::string_view no_seam;

// Contexts are in the order a player is likely to want them: where the
// party walks, then the bars and lists every screen shares, then the
// special screens, then what a shell adds.
//
// A row that is true of one program names it as its fourth word, and a
// screen only one program has names it on the context. Where the two
// programs differ on one key there are two rows, side by side.

constexpr std::array explore_rows{
    key_row{"Up, 8", "step forward one square"},
    key_row{"Left, Right, 4, 6", "turn a quarter turn left or right"},
    key_row{"Down, 2", "turn around"},
    key_row{"Home, End, 7, 1", "select the previous or next party member"},
    key_row{"M, Return on Move",
            "walk: the bar is Exit alone, and the arrows walk",
            "modern-controls"},
    key_row{"Return, Esc, E", "while walking: back to the bar, Move lit",
            "modern-controls"},
    key_row{"Left, Right", "at the bar: step the lit word; walking, they turn",
            "modern-controls"},
    key_row{"Up, Down",
            "at the bar: previous or next member; walking, they step",
            "modern-controls"},
    key_row{"Tab", "show or hide the map over the party list", "automap"},
};

constexpr std::array wilderness_rows{
    key_row{"Up, Right, Down, Left",
            "step one square north, east, south, west"},
    key_row{"Home, End, 7, 1", "select the previous or next party member"},
    key_row{"M, Return on Move",
            "walk: the bar is Exit alone, and the arrows walk",
            "modern-controls"},
    key_row{"Return, Esc, E", "while walking: back to the bar, Move lit",
            "modern-controls"},
    key_row{"Left, Right", "at the bar: step the lit word; walking, they step",
            "modern-controls"},
    key_row{"Up, Down",
            "at the bar: previous or next member; walking, they step",
            "modern-controls"},
};

// Curse of the Azure Bonds has no wilderness to walk: its overland is a
// bar of the script's and a list of the places the party may go.
constexpr std::array journey_rows{
    key_row{"its first letter", "take that command of the journey's bar"},
    key_row{", and .", "step the lit command left and right"},
    key_row{"Left, Right", "step the lit command", "modern-controls"},
    key_row{"Return", "take the lit command; in the list, go to the lit place"},
    key_row{"Home, End", "in the list of places: the place above or below"},
    key_row{"Up, Down", "in the list of places: as Home and End do",
            "modern-controls"},
    key_row{"Esc", "in the list of places: goes to the lit place too"},
};

constexpr std::array bar_rows{
    key_row{"its capital letter", "take that command; lower case works",
            no_seam, pool},
    key_row{"its first letter", "take that command; lower case works", no_seam,
            curse},
    key_row{", and .", "step the highlight left and right, wrapping"},
    key_row{"Left, Right", "step the highlight, except where they move or edit",
            "modern-controls"},
    key_row{"Up, Down, keypad 8, 2",
            "previous or next member where Home and End step it",
            "modern-controls"},
    key_row{"Up, Down at a door", "and the save bar: step the selected member",
            "modern-controls"},
    key_row{"Return", "take the highlighted command at nearly every bar",
            "modern-controls", pool},
    key_row{"Return", "take the highlighted command", no_seam, curse},
    key_row{"Y, N", "answer a Yes/No question"},
    key_row{"Esc", "leave camp, a list or a picker; not the exploring bar",
            no_seam, pool},
    key_row{"Esc", "leave camp or a shop's list; not the exploring bar",
            no_seam, curse},
    key_row{"Esc at Yes/No", "answer No to the question", "modern-controls"},
    key_row{"F", "on the camp bar: Fix, rest as long as the party needs",
            "encamp-fix", pool},
    key_row{"the selection", "a block, yellow: the lit word, row or member",
            "modern-controls", pool},
    key_row{"the selection", "a block, white: the lit word, row or member",
            "modern-controls", curse},
};

constexpr std::array menu_rows{
    key_row{"its capital letter", "take that command; lower case works",
            no_seam, pool},
    key_row{"its first letter", "take that command; lower case works", no_seam,
            curse},
    key_row{"Home, End", "select the previous or next party member"},
    key_row{"Up, Down", "move a cursor over the commands shown (not 8, 2)",
            "modern-controls"},
    key_row{"Return", "take the command under the cursor", "modern-controls"},
    key_row{"1-8 on the number row",
            "select that party member; the list shows each number",
            "modern-controls"},
    key_row{"the cursor row",
            "a block like every selection, its first letter white",
            "modern-controls", pool},
    key_row{"the cursor row", "a white block from end to end",
            "modern-controls", curse},
};

constexpr std::array list_rows{
    key_row{"Home, 7", "highlight the row above; wraps within the page"},
    key_row{"End, 1", "highlight the row below; wraps within the page"},
    key_row{"Up, Down, 8, 2",
            "highlight the row above or below, like Home and End",
            "modern-controls"},
    key_row{"Return", "choose the highlighted row"},
    key_row{"Return at Items", "take the lit command: Ready, until it is moved",
            "modern-controls", pool},
    key_row{"PgUp, PgDn, 9, 3",
            "turn the page, when the list has more than one"},
    key_row{"Esc, E", "leave the list (E is the bar's EXIT)", no_seam, pool},
    key_row{"E", "leave the list (the bar's EXIT)", no_seam, curse},
    key_row{"Esc", "leave a shop's list", no_seam, curse},
};

constexpr std::array picker_rows{
    key_row{"Home, 7", "the previous member"},
    key_row{"End, 1", "the next member"},
    key_row{"Up, Down, 8, 2", "the previous or next member", "modern-controls"},
    key_row{"S, Return", "choose this member"},
    key_row{"Return on EXIT", "choose nobody", "modern-controls"},
    key_row{"Esc, E", "choose nobody", no_seam, pool},
    key_row{"E", "choose nobody (the bar's EXIT)", no_seam, curse},
};

constexpr std::array combat_rows{
    key_row{"M, then the arrows", "step one square; into an enemy is an attack",
            no_seam, pool},
    key_row{"the arrows", "a turn opens on the move: step one square", no_seam,
            curse},
    key_row{"Home, PgUp, End, PgDn", "while moving: step NW, NE, SW, SE"},
    key_row{"Esc", "while moving: stop moving", no_seam, pool},
    key_row{"Esc", "while moving: the bar, and back where the move began",
            no_seam, curse},
    key_row{"an arrow at the bar", "start a move in that direction", no_seam,
            curse},
    key_row{"A", "Aim: N, P choose a target, M a cursor, E leaves"},
    key_row{"T", "in Aim: attack the target in range", no_seam, pool},
    key_row{"Q", "Quick: the computer plays this turn"},
    key_row{"D", "Done: Guard, Delay, Quit, Speed or Exit"},
};

constexpr std::array score_rows{
    key_row{"Up, Down, 8, 2", "previous or next score; hit points come last"},
    key_row{"Right, Left, 6, 4", "raise or lower the highlighted score"},
    key_row{"K", "keep the changes"},
    key_row{"Esc, E", "put the old scores back and leave"},
};

// Curse of the Azure Bonds' Modify has a name field among its scores, and
// steps its fields on Home and End.
constexpr std::array field_rows{
    key_row{"Home, End", "the field above or below: name, scores, hit points"},
    key_row{"Up, Down", "step the fields, as Home and End do",
            "modern-controls"},
    key_row{"Right, Left",
            "raise or lower the lit score; in the name, its cursor"},
    key_row{"K", "keep the changes"},
    key_row{"Esc, E", "put the old scores back and leave"},
};

constexpr std::array text_rows{
    key_row{"letters, digits", "typed in; lower case shows as capitals"},
    key_row{"Backspace", "erase the last character"},
    key_row{"Return, Esc", "accept what is typed; Esc does not cancel"},
    key_row{"arrows, Home, F-keys",
            "do nothing here, and no longer type letters", "modern-controls"},
};

constexpr std::array journal_rows{
    key_row{"N", "on the exploring bar: open the Notes log", "journal"},
    key_row{"Up, Down", "in the log: the entry above or below", "journal"},
    key_row{"Return", "open the entry under the cursor", "journal"},
    key_row{"N, P", "in an entry: next and previous page", "journal"},
    key_row{"Left, Right",
            "with modern-controls on: step the lit word of the bar", "journal"},
    key_row{"Return, bar lit",
            "take the lit command; in the log, after Left or Right", "journal"},
    key_row{"Esc", "close the entry, then the log", "journal"},
};

constexpr std::array desktop_rows{
    key_row{"F11", "mute or unmute"},
    key_row{"F12", "volume: 25, 50, 75, 100 percent, then round again"},
    key_row{"Pause/Break", "pull every triggered seam that is on"},
    key_row{"Right button", "the seam panel, then this card, then closed"},
    key_row{"Middle button", "the on-screen keyboard: each layout, then off"},
    key_row{"Left button", "press the on-screen key; toggle a panel row"},
    key_row{"Arrows, Return", "drive the keyboard or panel while one is up"},
};

constexpr std::array contexts{
    key_context{"explore", "Exploring: the 3D view (towns, dungeons)",
                explore_rows},
    key_context{"wilderness", "Exploring: the wilderness map", wilderness_rows,
                card_shell::both, pool},
    key_context{"journey", "Exploring: the overland journey", journey_rows,
                card_shell::both, curse},
    key_context{"bars", "Command bars (the line along the bottom)", bar_rows},
    key_context{"menu", "The main menu (party setup, training halls)",
                menu_rows},
    key_context{"lists", "Pick-lists (race, class, shops, spells, coins)",
                list_rows},
    key_context{"picker", "Choosing a party member", picker_rows},
    key_context{"combat", "Combat", combat_rows},
    key_context{"scores", "Modify: a new character's scores", score_rows,
                card_shell::both, pool},
    key_context{"fields", "Modify: a new character's scores and name",
                field_rows, card_shell::both, curse},
    key_context{"text", "Typing a name", text_rows},
    key_context{"journal", "Notes (the journal)", journal_rows},
    key_context{"desktop", "This window (the desktop host)", desktop_rows,
                card_shell::desktop},
};

constexpr std::string_view legend =
    "On the screens that use the arrows, the number row does what the "
    "keypad does: 8 2 4 6 are Up Down Left Right, 7 1 are Home End, 9 3 "
    "are PgUp PgDn. No keypad is needed. Where a row below names a seam, "
    "that seam changes it: with modern-controls on, walking is a mode the "
    "party's bar starts with Move and ends with Exit, and the number row's 1 "
    "to 8 pick a party member at the main menu.";

[[nodiscard]] std::string_view shell_word(card_shell shell) {
  switch (shell) {
    case card_shell::both:
      return "both";
    case card_shell::desktop:
      return "desktop";
    case card_shell::page:
      return "page";
  }
  return "both";
}

/// `text` as a JSON string. The suite holds every word of the card to
/// printable ASCII with no quote or backslash, so this escapes nothing
/// today; it still does, so that a word added later cannot break the
/// page's reading before the test says so.
void append_string(std::string& out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
}

/// The word of the edition an unqualified card is for: the baseline, the
/// first row of the edition table.
[[nodiscard]] std::string baseline_program() {
  return key_card_program(&machine::known_editions().front());
}

[[nodiscard]] std::string build_json() {
  std::string out;
  out += "{\"legend\":";
  append_string(out, legend);
  out += ",\"programs\":[";
  bool first_program = true;
  for (const machine::edition& program : machine::known_editions()) {
    if (!first_program) {
      out += ',';
    }
    first_program = false;
    out += "{\"word\":";
    append_string(out, key_card_program(&program));
    out += ",\"fingerprint\":";
    append_string(out, program.fingerprint);
    out += '}';
  }
  out += "],\"contexts\":[";
  bool first_context = true;
  for (const key_context& context : contexts) {
    if (!first_context) {
      out += ',';
    }
    first_context = false;
    out += "{\"id\":";
    append_string(out, context.id);
    out += ",\"title\":";
    append_string(out, context.title);
    out += ",\"shell\":";
    append_string(out, shell_word(context.shell));
    out += ",\"program\":";
    append_string(out, context.program);
    out += ",\"rows\":[";
    bool first_row = true;
    for (const key_row& row : context.rows) {
      if (!first_row) {
        out += ',';
      }
      first_row = false;
      out += "{\"keys\":";
      append_string(out, row.keys);
      out += ",\"does\":";
      append_string(out, row.does);
      out += ",\"seam\":";
      append_string(out, row.seam);
      out += ",\"program\":";
      append_string(out, row.program);
      out += '}';
    }
    out += "]}";
  }
  out += "]}";
  return out;
}

}  // namespace

std::span<const key_context> key_card() { return contexts; }

std::string_view key_card_legend() { return legend; }

std::string key_card_program(const machine::edition* program) {
  if (program == nullptr) {
    return {};
  }
  const std::string_view file = program->configuration.file;
  const std::string_view stem = file.substr(0, file.find('.'));
  std::string word;
  word.reserve(stem.size());
  for (const char c : stem) {
    word.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return word;
}

std::vector<key_page> key_card_for(
    card_shell shell, std::string_view program,
    const std::function<bool(std::string_view)>& seam_on) {
  // No program, or one this build does not recognize, is shown the
  // baseline's card: what every card was before a second program had one.
  const std::string baseline = baseline_program();
  const std::string_view running = program.empty() ? baseline : program;
  const auto for_running = [running](std::string_view named) {
    return named.empty() || named == running;
  };
  std::vector<key_page> pages;
  for (const key_context& context : contexts) {
    if (context.shell != card_shell::both && context.shell != shell) {
      continue;
    }
    if (!for_running(context.program)) {
      continue;
    }
    key_page page;
    page.context = &context;
    for (const key_row& row : context.rows) {
      if (for_running(row.program) && (row.seam.empty() || seam_on(row.seam))) {
        page.rows.push_back(&row);
      }
    }
    if (!page.rows.empty()) {
      pages.push_back(std::move(page));
    }
  }
  return pages;
}

std::string_view key_card_json() {
  static const std::string json = build_json();
  return json;
}

}  // namespace amberfolio::host
