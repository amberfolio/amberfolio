// SPDX-License-Identifier: AGPL-3.0-only
//
// The save-layer table, and the matcher that reads a path against it.
// save_layer.h has the reasoning; `docs/hosts.md` §6 has the runs the
// table was gathered from.

#include "amberfolio/machine/save_layer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition's save layer, watched rather than guessed: the
/// load menu asks the directory about `SAVE\SAVGAM<S>.DAT` for `S` from
/// `A` to `J` and about `SAVE\CHRDAT<S><N>.SAV` for `N` from 1 to 8, and
/// a save creates exactly the files below and no others.
///
/// Most specific pattern first — the three `<NAME>` rows are last
/// because each of them would otherwise match a member's record.
constexpr std::array<save_file, 13> baseline_files{{
    {.pattern = "SAVE\\SAVGAM<S>.DAT",
     .kind = save_file_kind::slot,
     .required = true,
     .about = "the saved game; the load menu learns which slots exist by "
              "opening this file for every letter in turn"},
    {.pattern = "SAVE\\CHRDAT<S><N>.SAV",
     .kind = save_file_kind::member,
     .required = true,
     .about = "one party member's record, written for every member of the "
              "party the save was taken from"},
    {.pattern = "SAVE\\CHRDAT<S><N>.ITM",
     .kind = save_file_kind::member,
     .required = false,
     .about = "what that member carries; a save that finds nothing to write "
              "deletes it rather than leaving an empty one"},
    {.pattern = "SAVE\\CHRDAT<S><N>.SPC",
     .kind = save_file_kind::member,
     .required = false,
     .about = "that member's memorized spells; absent for a member who casts "
              "none"},
    {.pattern = "SAVE\\CHARLIST.TXT",
     .kind = save_file_kind::roster,
     .required = false,
     .about = "the characters on the disk that are in no party; shared by "
              "every slot and written when one is made or taken up"},
    {.pattern = "SAVE\\AFMAP<S>.DAT",
     .kind = save_file_kind::sidecar,
     .required = false,
     .about = "this build's own: what the automap had explored when slot <S> "
              "was written"},
    {.pattern = "SAVE\\AFSEEN<S>.DAT",
     .kind = save_file_kind::sidecar,
     .required = false,
     .about = "this build's own: which journal entries the game had cited "
              "when slot <S> was written"},
    {.pattern = save_layer_automap_working,
     .kind = save_file_kind::sidecar,
     .required = false,
     .about = "this build's own: what the automap has explored right now, "
              "belonging to the playthrough rather than to a slot"},
    {.pattern = save_layer_journal_working,
     .kind = save_file_kind::sidecar,
     .required = false,
     .about = "this build's own: which journal entries the game has cited "
              "right now, belonging to the playthrough rather than to a slot"},
    {.pattern = "POOL.CFG",
     .kind = save_file_kind::config,
     .required = false,
     .about = "the program's settings, read at boot and written only by the "
              "configuration program beside it: a game file, not a "
              "playthrough's"},
    {.pattern = "SAVE\\<NAME>.CHA",
     .kind = save_file_kind::character,
     .required = false,
     .about = "a character kept under a name the player chose; a save moves "
              "the party's own into the slot and unlinks these"},
    {.pattern = "SAVE\\<NAME>.ITM",
     .kind = save_file_kind::character,
     .required = false,
     .about = "what that character carries"},
    {.pattern = "SAVE\\<NAME>.SPC",
     .kind = save_file_kind::character,
     .required = false,
     .about = "that character's memorized spells"},
}};

/// The baseline is the one edition this build has facts about
/// (`edition.h`), named by its program image the way a seam names one.
constexpr save_layer baseline{
    .fingerprint =
        "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",
    .slots = "ABCDEFGHIJ",
    .members = 8,
    .files = baseline_files,
};

constexpr std::array<const save_layer*, 1> table{{&baseline}};

/// The whole of one component's text. A `dos_name` is canonical and
/// upper-cased already, so nothing here folds case.
[[nodiscard]] std::string_view text_of(const dos_name& name) noexcept {
  const std::span<const char> chars = name.text();
  return {chars.data(), chars.size()};
}

/// A path as one piece of text, `SAVE\SAVGAMA.DAT`, so the matcher can
/// walk it against a pattern with the separator in it. Never longer than
/// a canonical path, which is what the buffer is sized on.
class path_text {
 public:
  explicit path_text(const dos_path& path) noexcept {
    for (std::size_t i = 0; i < path.depth(); ++i) {
      if (i != 0) {
        chars_[used_++] = '\\';
      }
      for (const char ch : text_of(path.component(i))) {
        chars_[used_++] = ch;
      }
    }
  }

  [[nodiscard]] std::string_view view() const noexcept {
    return {chars_.data(), used_};
  }

 private:
  std::array<char, max_host_path_text> chars_{};
  std::size_t used_{};
};

/// Whether `pattern` describes `text`, filling in what its placeholders
/// stood for.
///
/// One pass, no backtracking: every placeholder in this table is
/// followed by a literal that ends it — a slot letter and a member index
/// are one character each, and a `<NAME>` runs to the dot before the
/// extension — so there is never a choice to make and never a reason to
/// take one back.
[[nodiscard]] bool matches(std::string_view pattern, std::string_view text,
                           const save_layer& layer,
                           save_layer_row& found) noexcept {
  std::size_t p = 0;
  std::size_t t = 0;
  while (p < pattern.size()) {
    if (pattern.compare(p, 3, "<S>") == 0) {
      if (t >= text.size() ||
          layer.slots.find(text[t]) == std::string_view::npos) {
        return false;
      }
      found.slot = text[t];
      p += 3;
      t += 1;
    } else if (pattern.compare(p, 3, "<N>") == 0) {
      if (t >= text.size() || text[t] < '1' ||
          text[t] > static_cast<char>('0' + layer.members)) {
        return false;
      }
      found.member = static_cast<std::uint8_t>(text[t] - '0');
      p += 3;
      t += 1;
    } else if (pattern.compare(p, 6, "<NAME>") == 0) {
      const std::size_t began = t;
      while (t < text.size() && text[t] != '.' && text[t] != '\\') {
        ++t;
      }
      const std::size_t length = t - began;
      if (length == 0 || length > 8) {
        return false;
      }
      p += 6;
    } else {
      if (t >= text.size() || text[t] != pattern[p]) {
        return false;
      }
      ++p;
      ++t;
    }
  }
  return t == text.size();
}

}  // namespace

const char* save_file_kind_name(save_file_kind kind) noexcept {
  switch (kind) {
    case save_file_kind::member:
      return "member";
    case save_file_kind::roster:
      return "roster";
    case save_file_kind::character:
      return "character";
    case save_file_kind::config:
      return "config";
    case save_file_kind::sidecar:
      return "sidecar";
    case save_file_kind::slot:
      break;
  }
  return "slot";
}

const save_layer* save_layer_for(const sha256_digest& program) noexcept {
  for (const save_layer* known : table) {
    if (digest_is(program, known->fingerprint)) {
      return known;
    }
  }
  return nullptr;
}

save_layer_row match_save_file(const save_layer& layer,
                               const dos_path& path) noexcept {
  const path_text text(path);
  for (std::size_t i = 0; i < layer.files.size(); ++i) {
    save_layer_row found;
    if (matches(layer.files[i].pattern, text.view(), layer, found)) {
      found.file = &layer.files[i];
      found.index = i;
      return found;
    }
  }
  return {};
}

}  // namespace amberfolio::machine
