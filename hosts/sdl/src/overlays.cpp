// SPDX-License-Identifier: AGPL-3.0-only

#include "overlays.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/screen_keyboard.h"
#include "key_card_view.h"
#include "launch_config.h"
#include "options.h"
#include "screen_keyboard_view.h"
#include "seam_host.h"
#include "seam_panel.h"

namespace amberfolio::sdl {

namespace osk = machine::screen_keyboard;

namespace {

/// The keys the keyboard takes for itself while it is up: four to move
/// the focus and two to commit. A key it took the make of is a key it
/// also takes the break of — swallowing an unmatched break would be an
/// input the player never made, and posting one whose make it ate would
/// leave the machine holding a key nobody is pressing.
struct keyboard_control {
  SDL_Scancode code;
  osk::nav where;
  bool commits;
};
constexpr std::array<keyboard_control, 6> keyboard_controls{{
    {.code = SDL_SCANCODE_LEFT, .where = osk::nav::left, .commits = false},
    {.code = SDL_SCANCODE_RIGHT, .where = osk::nav::right, .commits = false},
    {.code = SDL_SCANCODE_UP, .where = osk::nav::up, .commits = false},
    {.code = SDL_SCANCODE_DOWN, .where = osk::nav::down, .commits = false},
    // The two that commit carry a direction they never use: an
    // aggregate has to fill every field, and `left` is as arbitrary as
    // any of them.
    {.code = SDL_SCANCODE_RETURN, .where = osk::nav::left, .commits = true},
    {.code = SDL_SCANCODE_KP_ENTER, .where = osk::nav::left, .commits = true},
}};
static_assert(keyboard_controls.size() == keyboard_control_count);

[[nodiscard]] std::size_t keyboard_control_of(SDL_Scancode code) {
  for (std::size_t i = 0; i < keyboard_controls.size(); ++i) {
    if (keyboard_controls[i].code == code) {
      return i;
    }
  }
  return keyboard_controls.size();
}

/// The keys the panel takes while it is up and the keyboard is not.
/// `keyboard_control`'s make/break rule applies here for its reason:
/// the break of a key whose make was taken is taken too.
struct panel_control {
  SDL_Scancode code;
  int step;
  bool toggles;
};

constexpr std::array<panel_control, 4> panel_controls{{
    {.code = SDL_SCANCODE_UP, .step = -1, .toggles = false},
    {.code = SDL_SCANCODE_DOWN, .step = 1, .toggles = false},
    // The two that toggle carry a step they never use, for the reason
    // the keyboard's two commits carry a direction they never use.
    {.code = SDL_SCANCODE_RETURN, .step = 0, .toggles = true},
    {.code = SDL_SCANCODE_KP_ENTER, .step = 0, .toggles = true},
}};
static_assert(panel_controls.size() == panel_control_count);

[[nodiscard]] std::size_t panel_control_of(SDL_Scancode code) {
  for (std::size_t i = 0; i < panel_controls.size(); ++i) {
    if (panel_controls[i].code == code) {
      return i;
    }
  }
  return panel_controls.size();
}

}  // namespace

overlays::overlays(machine::machine& box, const options& opts, bool replaying,
                   key_sink post_key, std::vector<std::string> document_notice)
    : box_(box),
      opts_(opts),
      replaying_(replaying),
      post_key_(std::move(post_key)),
      keyboard_layouts_(osk::layouts()),
      panel_shown_(opts.seam_panel),
      panel_focus_(box.seams().count() == 0 ? sdl::panel_no_row : 0),
      document_notice_(std::move(document_notice)) {
  if (!opts_.keyboard.empty()) {
    for (std::size_t i = 0; i < keyboard_layouts_.size(); ++i) {
      if (keyboard_layouts_[i].name == opts_.keyboard) {
        keyboard_index_ = i;
        keyboard_shown_ = true;
      }
    }
  }
  if (keyboard_shown_) {
    keyboard_focus_ = osk::default_focus(keyboard_layouts_[keyboard_index_]);
  }
}

void overlays::keyboard_unlatch() {
  const osk::commit made = osk::release_latched(keyboard_latched_);
  for (std::size_t i = 0; i < made.count; ++i) {
    post_key_(made.events[i].scancode, made.events[i].down
                                           ? machine::key_action::down
                                           : machine::key_action::up);
  }
  keyboard_latched_ = 0;
}

void overlays::keyboard_step() {
  keyboard_unlatch();
  if (!keyboard_shown_) {
    keyboard_index_ = 0;
    keyboard_shown_ = true;
  } else if (keyboard_index_ + 1 < keyboard_layouts_.size()) {
    ++keyboard_index_;
  } else {
    keyboard_shown_ = false;
  }
  keyboard_focus_ = keyboard_shown_
                        ? osk::default_focus(keyboard_layouts_[keyboard_index_])
                        : osk::no_key;
  keyboard_repaint_ = true;
}

void overlays::keyboard_commit(std::size_t at) {
  keyboard_focus_ = at;
  const osk::commit made = osk::commit_key(keyboard_layouts_[keyboard_index_],
                                           at, keyboard_latched_);
  for (std::size_t i = 0; i < made.count; ++i) {
    post_key_(made.events[i].scancode, made.events[i].down
                                           ? machine::key_action::down
                                           : machine::key_action::up);
  }
  keyboard_latched_ = made.latched;
  keyboard_repaint_ = true;
}

void overlays::panel_toggle(std::size_t at) {
  const std::vector<sdl::panel_row> rows = sdl::panel_rows(box_.seams());
  if (at >= rows.size() || !rows[at].available) {
    return;
  }
  // By value: `rows` is gone by the end of this call and the id is a
  // view into a string inside it.
  const std::string id = rows[at].id;
  const bool want_on = !rows[at].on;
  panel_repaint_ = true;
  if (replaying_) {
    // The panel is a view during a replay and nothing more, for the
    // reason a keystroke at the window is refused: a recording's seams
    // are its own initial condition (docs/replay.md).
    std::fprintf(stderr, "amberfolio: a replay's seams are the recording's\n");
    return;
  }
  const machine::seam_error why =
      want_on ? box_.seams().enable(id) : box_.seams().disable(id);
  if (why != machine::seam_error::none) {
    std::fprintf(stderr, "amberfolio: seam %s %s refused (%s)\n", id.c_str(),
                 want_on ? "on" : "off", seam_refusal(why));
    return;
  }
  // Nothing is said about a toggle that took: the engine's own edge log
  // already prints `seam automap on` and `seam automap armed`
  // (`drain_edges()`), and a second sentence from this host would be
  // the same transition twice. What follows says the other half — that
  // the choice was written down.
  remember_panel_seams(opts_, box_.seams());
}

void overlays::panel_move(int step) {
  const std::size_t rows = box_.seams().count();
  panel_repaint_ = true;
  if (card_shown_) {
    const std::size_t pages = sdl::card_pages(box_.seams()).size();
    if (pages != 0) {
      card_page_ = (card_page_ + (step < 0 ? pages - 1 : 1)) % pages;
    }
    return;
  }
  if (rows == 0) {
    panel_focus_ = sdl::panel_no_row;
    return;
  }
  if (panel_focus_ >= rows) {
    panel_focus_ = step < 0 ? rows - 1 : 0;
    return;
  }
  if (step < 0) {
    panel_focus_ = panel_focus_ == 0 ? rows - 1 : panel_focus_ - 1;
  } else {
    panel_focus_ = panel_focus_ + 1 == rows ? 0 : panel_focus_ + 1;
  }
}

void overlays::focus_lost() {
  // Including whatever the on-screen keyboard had latched: the
  // breaks have just gone out with the rest, so the mask has to
  // stop claiming they are down (#377).
  keyboard_latched_ = 0;
  keyboard_taken_.fill(false);
  keyboard_repaint_ = true;
  panel_taken_.fill(false);
}

void overlays::document_presented(std::vector<std::string> notice) {
  document_notice_ = std::move(notice);
  panel_shown_ = true;
  card_shown_ = false;
  if (panel_focus_ == sdl::panel_no_row && box_.seams().count() != 0) {
    panel_focus_ = 0;
  }
  panel_repaint_ = true;
}

void overlays::mouse_button_down(const SDL_Event& event,
                                 SDL_Renderer* renderer) {
  // The middle button steps the on-screen keyboard on (#377), the
  // right one opens and closes the toggle panel (#383), and the
  // left one presses whatever is under the pointer. This machine
  // has no mouse, so none of the three is taken from anything.
  if (event.button.button == SDL_BUTTON_MIDDLE && !replaying_) {
    keyboard_step();
  } else if (event.button.button == SDL_BUTTON_RIGHT) {
    // Hidden, then the seams, then the key card, then hidden.
    const bool cards = panel_shown_ && !card_shown_;
    panel_shown_ = !panel_shown_ || !card_shown_;
    card_shown_ = cards;
    if (panel_shown_ && panel_focus_ == sdl::panel_no_row &&
        box_.seams().count() != 0) {
      panel_focus_ = 0;
    }
    panel_repaint_ = true;
  } else if (event.button.button == SDL_BUTTON_LEFT) {
    int window_width = 0;
    int window_height = 0;
    const bool sized =
        SDL_GetRenderOutputSize(renderer, &window_width, &window_height);
    // **The panel is asked first.** The two overlays can be over
    // the same pixel, and a click belongs to the one in front;
    // a click that landed on no row of the panel falls through
    // to the keyboard rather than being swallowed.
    bool taken_by_panel = false;
    if (sized && panel_shown_ && card_shown_) {
      panel_move(1);  // a click on the card turns its page
      taken_by_panel = true;
    } else if (sized && panel_shown_) {
      const std::vector<sdl::panel_row> rows = sdl::panel_rows(box_.seams());
      const std::size_t at = sdl::row_under(
          sdl::fit_panel(sdl::panel_lines(rows, panel_focus_, document_notice_),
                         window_width, window_height),
          rows.size(), event.button.x, event.button.y);
      if (at != sdl::panel_no_row) {
        panel_focus_ = at;
        panel_toggle(at);
        taken_by_panel = true;
      }
    }
    if (sized && !taken_by_panel && keyboard_shown_ && !replaying_) {
      const osk::layout& shown = keyboard_layouts_[keyboard_index_];
      const std::size_t at =
          sdl::key_under(sdl::fit_keyboard(shown, window_width, window_height),
                         shown, event.button.x, event.button.y);
      if (at != osk::no_key) {
        keyboard_commit(at);
      }
    }
  }
}

bool overlays::take_key(const SDL_Event& event) {
  const bool down = event.type == SDL_EVENT_KEY_DOWN;
  // While the on-screen keyboard is up it owns the four arrows
  // and Return: they move its focus and commit the key under it,
  // which is the path a gamepad will drive in M8 (#210) and is
  // proven here with the only four-way control a desktop has. A
  // person with a keyboard in front of them steps the overlay off
  // and gets them back; a person without one never had them.
  //
  // The break of a key it took the make of is taken too, and the
  // break of one it did not is not: stepping the overlay on
  // between the two would otherwise leave the machine holding a
  // key nobody is pressing.
  //
  // A *repeat* is not a make, and follows whoever took the make:
  // the overlay stepping on or off under a held key must not
  // send the repeats to a different owner than the first make
  // went to, or the machine is left holding a key whose break
  // was swallowed (#426).
  const bool follows_make = !down || event.key.repeat;
  const std::size_t control = keyboard_control_of(event.key.scancode);
  const bool owned =
      control < keyboard_controls.size() &&
      (follows_make ? keyboard_taken_[control] : keyboard_shown_);
  // And the panel takes up, down and Return while it is up and
  // the keyboard is not (#383) — asked first, because the same
  // break has to reach whichever overlay took its make.
  const std::size_t picked = panel_control_of(event.key.scancode);
  const bool panel_owned = picked < panel_controls.size() &&
                           (follows_make ? panel_taken_[picked]
                                         : (panel_shown_ && !keyboard_shown_));
  if (panel_owned && down) {
    panel_taken_[picked] = true;
    if (!event.key.repeat) {
      if (panel_controls[picked].toggles) {
        if (panel_focus_ != sdl::panel_no_row && !card_shown_) {
          panel_toggle(panel_focus_);
        }
      } else {
        panel_move(panel_controls[picked].step);
      }
    }
    return true;
  }
  if (panel_owned) {
    panel_taken_[picked] = false;
    return true;
  }
  if (owned && down) {
    keyboard_taken_[control] = true;
    if (!event.key.repeat && !replaying_) {
      if (keyboard_controls[control].commits) {
        if (keyboard_focus_ != osk::no_key) {
          keyboard_commit(keyboard_focus_);
        }
      } else {
        keyboard_focus_ =
            osk::move(keyboard_layouts_[keyboard_index_], keyboard_focus_,
                      keyboard_controls[control].where);
        keyboard_repaint_ = true;
      }
    }
    return true;
  }
  if (owned) {
    keyboard_taken_[control] = false;
    return true;
  }
  return false;
}

void overlays::draw(SDL_Renderer* renderer) {
  if (keyboard_shown_) {
    keyboard_paint_.draw(renderer, keyboard_layouts_[keyboard_index_],
                         keyboard_focus_, keyboard_latched_);
  }
  // The panel over the keyboard, because the keyboard is what a
  // person uses to drive the panel and the panel is what they came
  // to read. Its rows are built here, from the engine, at every
  // paint: `fired` is only a live number if nothing caches it.
  if (panel_shown_) {
    panel_paint_.draw(
        renderer,
        card_shown_ ? sdl::card_lines(sdl::card_pages(box_.seams()), card_page_)
                    : sdl::panel_lines(sdl::panel_rows(box_.seams()),
                                       panel_focus_, document_notice_),
        card_shown_ ? sdl::panel_no_row : panel_focus_);
  }
  keyboard_repaint_ = false;
  panel_repaint_ = false;
}

}  // namespace amberfolio::sdl
