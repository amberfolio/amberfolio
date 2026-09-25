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
/// load menu asks the save directory about `SAVGAM<S>.DAT` for `S` from
/// `A` to `J` and about `CHRDAT<S><N>.SAV` for `N` from 1 to 8, and a
/// save creates exactly the files below and no others.
///
/// Most specific pattern first — the three `<NAME>` rows are last
/// because each of them would otherwise match a member's record, and the
/// configuration file is ahead of them because on a copy that saves
/// beside its game files it is in the save directory too.
constexpr std::array<save_file, 13> baseline_files{{
    {.pattern = "SAVGAM<S>.DAT",
     .kind = save_file_kind::slot,
     .required = true,
     .about = "the saved game; the load menu learns which slots exist by "
              "opening this file for every letter in turn"},
    {.pattern = "CHRDAT<S><N>.SAV",
     .kind = save_file_kind::member,
     .required = true,
     .about = "one party member's record, written for every member of the "
              "party the save was taken from"},
    {.pattern = "CHRDAT<S><N>.ITM",
     .kind = save_file_kind::member,
     .required = false,
     .about = "what that member carries; a save that finds nothing to write "
              "deletes it rather than leaving an empty one"},
    {.pattern = "CHRDAT<S><N>.SPC",
     .kind = save_file_kind::member,
     .required = false,
     .about = "that member's memorized spells; absent for a member who casts "
              "none"},
    {.pattern = "CHARLIST.TXT",
     .kind = save_file_kind::roster,
     .required = false,
     .about = "the characters on the disk that are in no party; shared by "
              "every slot and written when one is made or taken up"},
    {.pattern = "AFMAP<S>.DAT",
     .kind = save_file_kind::sidecar,
     .required = false,
     .about = "this build's own: what the automap had explored when slot <S> "
              "was written"},
    {.pattern = "AFSEEN<S>.DAT",
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
    {.pattern = save_layer_config_file,
     .kind = save_file_kind::config,
     .required = false,
     .about = "the program's settings, read at boot and written only by the "
              "configuration program beside it: a game file, not a "
              "playthrough's",
     .anchor = save_file_anchor::current_directory},
    {.pattern = "<NAME>.CHA",
     .kind = save_file_kind::character,
     .required = false,
     .about = "a character kept under a name the player chose; a save moves "
              "the party's own into the slot and unlinks these"},
    {.pattern = "<NAME>.ITM",
     .kind = save_file_kind::character,
     .required = false,
     .about = "what that character carries"},
    {.pattern = "<NAME>.SPC",
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

/// Whether `path` is directly inside `directory`: one component deeper,
/// and every component above its leaf the same.
[[nodiscard]] bool directly_inside(const dos_path& path,
                                   const dos_path& directory) noexcept {
  return path.depth() == directory.depth() + 1 && path.parent() == directory;
}

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
                               const save_layer_places& places,
                               const dos_path& path) noexcept {
  if (path.is_root()) {
    return {};
  }
  const std::string_view leaf = text_of(path.leaf());
  for (std::size_t i = 0; i < layer.files.size(); ++i) {
    const save_file& row = layer.files[i];
    if (!directly_inside(path, places.of(row.anchor))) {
      continue;
    }
    save_layer_row found;
    if (matches(row.pattern, leaf, layer, found)) {
      found.file = &row;
      found.index = i;
      return found;
    }
  }
  return {};
}

std::size_t spell_save_pattern(const save_file& file,
                               const save_layer_places& places,
                               std::span<char> out) noexcept {
  std::size_t used = 0;
  const auto put = [&](char ch) {
    if (used + 1 < out.size()) {
      out[used] = ch;
    }
    ++used;
  };
  const dos_path& directory = places.of(file.anchor);
  for (std::size_t i = 0; i < directory.depth(); ++i) {
    for (const char ch : text_of(directory.component(i))) {
      put(ch);
    }
    put('\\');
  }
  for (const char ch : file.pattern) {
    put(ch);
  }
  if (out.empty() || used + 1 > out.size()) {
    if (!out.empty()) {
      out[0] = '\0';
    }
    return 0;
  }
  out[used] = '\0';
  return used;
}

const char* save_directory_trouble_name(save_directory_trouble what) noexcept {
  switch (what) {
    case save_directory_trouble::no_config:
      return "no-config";
    case save_directory_trouble::unreadable:
      return "unreadable";
    case save_directory_trouble::too_short:
      return "too-short";
    case save_directory_trouble::not_a_path:
      return "not-a-path";
    case save_directory_trouble::none:
      break;
  }
  return "none";
}

save_directory_answer read_save_directory(filesystem& fs,
                                          const dos_path& current_directory) {
  save_directory_answer answer;
  const vfs_result<dos_path> config = canonicalize(
      current_directory, std::span<const char>(save_layer_config_file.data(),
                                               save_layer_config_file.size()));
  if (!config.ok() || !fs.exists(config.value)) {
    answer.trouble = save_directory_trouble::no_config;
    return answer;
  }

  // The line wanted is short and near the top; this is far more than the
  // four lines before its end have ever needed. A line that runs off the
  // end of it is not read as if it had ended there.
  std::array<std::uint8_t, 512> bytes{};
  const vfs_result<file_handle> file =
      fs.open(config.value, open_mode::read_only);
  if (!file.ok()) {
    answer.trouble = save_directory_trouble::unreadable;
    return answer;
  }
  const vfs_result<std::size_t> got = fs.read(file.value, bytes);
  static_cast<void>(fs.close(file.value));
  if (!got.ok()) {
    answer.trouble = save_directory_trouble::unreadable;
    return answer;
  }

  // Lines end at LF; a CR before it is part of the ending, not the line.
  std::size_t line = 1;
  std::size_t begins = 0;
  std::size_t ends = got.value;
  bool terminated = false;
  for (std::size_t i = 0; i < got.value; ++i) {
    if (bytes[i] != '\n') {
      continue;
    }
    if (line == save_layer_config_save_line) {
      ends = i;
      terminated = true;
      break;
    }
    ++line;
    begins = i + 1;
  }
  const bool whole_file = got.value < bytes.size();
  if (line != save_layer_config_save_line || (!terminated && !whole_file)) {
    answer.trouble = save_directory_trouble::too_short;
    return answer;
  }
  if (ends > begins && bytes[ends - 1] == '\r') {
    --ends;
  }
  if (ends == begins) {
    answer.trouble = save_directory_trouble::too_short;
    return answer;
  }

  std::array<char, 512> text{};
  for (std::size_t i = begins; i < ends; ++i) {
    text[i - begins] = static_cast<char>(bytes[i]);
  }
  const vfs_result<dos_path> directory = canonicalize(
      current_directory, std::span<const char>(text.data(), ends - begins));
  if (!directory.ok()) {
    answer.trouble = save_directory_trouble::not_a_path;
    return answer;
  }
  answer.directory = directory.value;
  answer.trouble = save_directory_trouble::none;
  return answer;
}

bool save_layer_places_of(filesystem& fs, const dos_path& current_directory,
                          save_layer_places& places) {
  const save_directory_answer saves =
      read_save_directory(fs, current_directory);
  if (!saves.ok()) {
    return false;
  }
  places.save_directory = saves.directory;
  places.current_directory = current_directory;
  return true;
}

}  // namespace amberfolio::machine
