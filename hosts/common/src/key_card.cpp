// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card's table (#427). key_card.h says what it is and what is
// not on it; this file is the data and the two readings of it.
//
// **To add a seam's keys, add a line in the context where the key is
// pressed** and name the seam as the third word. The row shows only while
// that seam is on, and the unit suite checks the name against
// `all_seams()`.
//
// How each row was checked is in docs/hosts.md: driven against the
// running program where that is cheap, and against the routine that
// reads the key where it is not.

#include "amberfolio/host/key_card.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amberfolio::host {
namespace {

// Contexts are in the order a player is likely to want them: where the
// party walks, then the bars and lists every screen shares, then the
// special screens, then what a shell adds.

constexpr std::array explore_rows{
    key_row{"Up, 8", "step forward one square"},
    key_row{"Left, Right, 4, 6", "turn a quarter turn left or right"},
    key_row{"Down, 2", "turn around"},
    key_row{"Home, End, 7, 1", "select the previous or next party member"},
    key_row{"1-8 on the number row",
            "select that party member; keypad 1-8 keep their keys",
            "hero-keys"},
    key_row{"Tab", "show or hide the map over the party list", "automap"},
};

constexpr std::array wilderness_rows{
    key_row{"Up, Right, Down, Left",
            "step one square north, east, south, west"},
    key_row{"Home, End, 7, 1", "select the previous or next party member"},
    key_row{"1-8 on the number row",
            "select that party member; keypad 1-8 keep their keys",
            "hero-keys"},
};

constexpr std::array bar_rows{
    key_row{"its capital letter", "take that command; lower case works"},
    key_row{", and .", "step the highlight left and right, wrapping"},
    key_row{"Left, Right", "step the highlight, except where they move or edit",
            "bar-keys"},
    key_row{"Up, Down", "previous or next member where Home and End step it",
            "list-arrows"},
    key_row{"Return",
            "take the highlighted command: questions, camp, exploring",
            "bar-keys"},
    key_row{"1-8 on the number row",
            "camp, menus, shops: select that party member", "hero-keys"},
    key_row{"Y, N", "answer a Yes/No question"},
    key_row{"Esc", "leave camp, a list or a picker; not the exploring bar"},
    key_row{"Esc at Yes/No", "answer No to the question", "bar-keys"},
    key_row{"F", "on the camp bar: Fix, rest as long as the party needs",
            "encamp-fix"},
};

constexpr std::array menu_rows{
    key_row{"its capital letter", "take that command; lower case works"},
    key_row{"Home, End", "select the previous or next party member"},
    key_row{"Up, Down", "move a cursor over the commands shown (not 8, 2)",
            "menu-cursor"},
    key_row{"Return", "take the command under the cursor, once one shows",
            "menu-cursor"},
};

constexpr std::array list_rows{
    key_row{"Home, 7", "highlight the row above; wraps within the page"},
    key_row{"End, 1", "highlight the row below; wraps within the page"},
    key_row{"Up, Down, 8, 2",
            "highlight the row above or below, like Home and End",
            "list-arrows"},
    key_row{"Return", "choose the highlighted row"},
    key_row{"PgUp, PgDn, 9, 3",
            "turn the page, when the list has more than one"},
    key_row{"Esc, E", "leave the list (E is the bar's EXIT)"},
};

constexpr std::array picker_rows{
    key_row{"Home, 7", "the previous member"},
    key_row{"End, 1", "the next member"},
    key_row{"Up, Down, 8, 2", "the previous or next member", "list-arrows"},
    key_row{"S, Return", "choose this member"},
    key_row{"Esc, E", "choose nobody"},
};

constexpr std::array combat_rows{
    key_row{"M, then the arrows",
            "step one square; into an enemy is an attack"},
    key_row{"Home, PgUp, End, PgDn", "while moving: step NW, NE, SW, SE"},
    key_row{"Esc", "while moving: stop moving"},
    key_row{"A", "Aim: N, P choose a target, M a cursor, E leaves"},
    key_row{"T", "in Aim: attack the target in range"},
    key_row{"Q", "Quick: the computer plays this turn"},
    key_row{"D", "Done: Guard, Delay, Quit, Speed or Exit"},
};

constexpr std::array score_rows{
    key_row{"Up, Down, 8, 2", "previous or next score; hit points come last"},
    key_row{"Right, Left, 6, 4", "raise or lower the highlighted score"},
    key_row{"K", "keep the changes"},
    key_row{"Esc, E", "put the old scores back and leave"},
};

constexpr std::array text_rows{
    key_row{"letters, digits", "typed in; lower case shows as capitals"},
    key_row{"Backspace", "erase the last character"},
    key_row{"Return, Esc", "accept what is typed; Esc does not cancel"},
};

constexpr std::array journal_rows{
    key_row{"N", "on the exploring bar: open the Notes log", "journal"},
    key_row{"Up, Down", "in the log: the entry above or below", "journal"},
    key_row{"Return", "open the entry under the cursor", "journal"},
    key_row{"N, P", "in an entry: next and previous page", "journal"},
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
    key_context{"wilderness", "Exploring: the wilderness map", wilderness_rows},
    key_context{"bars", "Command bars (the line along the bottom)", bar_rows},
    key_context{"menu", "The main menu (party setup, training halls)",
                menu_rows},
    key_context{"lists", "Pick-lists (race, class, shops, spells, coins)",
                list_rows},
    key_context{"picker", "Choosing a party member", picker_rows},
    key_context{"combat", "Combat", combat_rows},
    key_context{"scores", "Modify: a new character's scores", score_rows},
    key_context{"text", "Typing a name", text_rows},
    key_context{"journal", "Notes (the journal)", journal_rows},
    key_context{"desktop", "This window (the desktop host)", desktop_rows,
                card_shell::desktop},
};

constexpr std::string_view legend =
    "On the screens that use the arrows, the number row does what the "
    "keypad does: 8 2 4 6 are Up Down Left Right, 7 1 are Home End, 9 3 "
    "are PgUp PgDn. No keypad is needed. Where a row below names a seam, "
    "that seam changes it: with hero-keys on, the number row's 1 to 8 pick "
    "a party member on the screens that show the party list.";

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

[[nodiscard]] std::string build_json() {
  std::string out;
  out += "{\"legend\":";
  append_string(out, legend);
  out += ",\"contexts\":[";
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

std::vector<key_page> key_card_for(
    card_shell shell, const std::function<bool(std::string_view)>& seam_on) {
  std::vector<key_page> pages;
  for (const key_context& context : contexts) {
    if (context.shell != card_shell::both && context.shell != shell) {
      continue;
    }
    key_page page;
    page.context = &context;
    for (const key_row& row : context.rows) {
      if (row.seam.empty() || seam_on(row.seam)) {
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
