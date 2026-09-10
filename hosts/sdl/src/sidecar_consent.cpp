// SPDX-License-Identifier: AGPL-3.0-only
//
// The question about the sidecars, its text and its answer (#385).
// `sidecar_consent.h` has the reasoning; this is the three functions
// under it.

#include "sidecar_consent.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace amberfolio::sdl {
namespace {

/// The question itself.
///
/// Written to be read once, by somebody who has just unpacked this and
/// wants to play a game: what is being asked for, where it lands, whose
/// directory that is, and what does *not* happen. The names of the two
/// files are in it because "beside your saves" is not something a person
/// can go and look at afterwards, and a file they can find is a file they
/// can delete.
constexpr std::array<std::string_view, 8> question{
    "may this build keep your progress beside your saved games?",
    "  Two of the enhancements learn something as you play: which streets",
    "  the automap has drawn for you, and which journal entries the game",
    "  has sent you to. Neither survives the machine stopping.",
    "  Kept, they go in \\SAVE\\AFMAP.DAT and \\SAVE\\AFSEEN.DAT - files of",
    "  this project's own, in your game directory, beside your saves and",
    "  never inside one. Your own files are never written to, and neither",
    "  of these appears at all until there is something to put in it.",
};

constexpr std::string_view prompt = "keep them beside my saves? [y/n] ";

/// The spaces, tabs and carriage returns off both ends. A player's answer
/// arrives with whatever their terminal put on the end of it.
[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept {
  const auto blank = [](char one) {
    return one == ' ' || one == '\t' || one == '\r' || one == '\n';
  };
  while (!text.empty() && blank(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && blank(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

/// One ASCII letter, folded down. Nothing here is locale-aware and
/// nothing here needs to be: the four words this accepts are ASCII.
[[nodiscard]] constexpr char lowered(char one) noexcept {
  return (one >= 'A' && one <= 'Z')
             ? static_cast<char>(one - 'A' + 'a')
             : one;
}

/// `text` against `word`, case-folded, whole.
[[nodiscard]] bool same_word(std::string_view text,
                             std::string_view word) noexcept {
  if (text.size() != word.size()) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (lowered(text[i]) != word[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

const char* sidecar_ask_name(sidecar_ask why) noexcept {
  switch (why) {
    case sidecar_ask::ask:
      return "ask";
    case sidecar_ask::said_on_the_command_line:
      return "said-on-the-command-line";
    case sidecar_ask::already_answered:
      return "already-answered";
    case sidecar_ask::nowhere_to_remember:
      return "nowhere-to-remember";
    case sidecar_ask::nobody_is_asked:
      return "nobody-is-asked";
    case sidecar_ask::nothing_to_write_beside:
      return "nothing-to-write-beside";
  }
  return "unknown";
}

sidecar_ask should_ask_about_sidecars(const sidecar_run& run) noexcept {
  // Flag first, and the same precedence `prefer()` keeps: a launch that
  // named the flag has answered the question for itself, and asking it
  // anyway would be asking somebody something they have just said.
  if (run.named_on_the_command_line) {
    return sidecar_ask::said_on_the_command_line;
  }
  // Then what was remembered, which is the whole of "asked once".
  if (run.config_answered) {
    return sidecar_ask::already_answered;
  }
  // Then whether this is a run with a person in it. Before the two
  // conditions below, because it is the one that must never be got
  // wrong: a driven run that stopped at a prompt would hang, and one
  // that answered its own prompt would write into a pinned disk.
  if (run.driven) {
    return sidecar_ask::nobody_is_asked;
  }
  if (!run.have_game) {
    return sidecar_ask::nothing_to_write_beside;
  }
  if (!run.can_remember) {
    return sidecar_ask::nowhere_to_remember;
  }
  return sidecar_ask::ask;
}

std::span<const std::string_view> sidecar_question() noexcept {
  return question;
}

std::string_view sidecar_prompt() noexcept { return prompt; }

std::optional<bool> read_sidecar_answer(std::string_view line) noexcept {
  const std::string_view said = trimmed(line);
  if (same_word(said, "y") || same_word(said, "yes")) {
    return true;
  }
  if (same_word(said, "n") || same_word(said, "no")) {
    return false;
  }
  return std::nullopt;
}

}  // namespace amberfolio::sdl
