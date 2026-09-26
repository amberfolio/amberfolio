// SPDX-License-Identifier: AGPL-3.0-only

#include "rehash.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/state.h"

namespace amberfolio::sdl {

void rehash_collector::apply(machine::replay_player& player,
                             machine::machine& box) {
  const std::size_t before = player.checkpoints_verified();
  static_cast<void>(player.apply(box));
  if (player.rehashing() && player.checkpoints_verified() != before) {
    std::array<char, machine::replay_max_line> line{};
    const std::size_t n = machine::format_replay_line(player.rehashed(), line);
    lines_.emplace_back(line.data(), n);
  }
}

std::optional<std::string> rehash_text(
    std::string_view text, const std::vector<std::string>& checkpoints,
    std::string_view& why) {
  std::string out;
  std::size_t next = 0;
  bool first = true;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::string_view line =
        text.substr(0, end == std::string_view::npos ? text.size() : end + 1);
    text.remove_prefix(line.size());
    if (first) {
      first = false;
      const std::size_t at = line.find(" state=");
      if (at == std::string_view::npos) {
        why = "no header to rewrite";
        return std::nullopt;
      }
      out.append(line.substr(0, at));
      out.append(" state=");
      out.append(std::to_string(machine::state_format_version));
      out.push_back('\n');
      continue;
    }
    if (line.starts_with("checkpoint ")) {
      if (next == checkpoints.size()) {
        why = "more checkpoints than were taken";
        return std::nullopt;
      }
      out.append(checkpoints[next]);
      ++next;
      continue;
    }
    out.append(line);
  }
  if (next != checkpoints.size()) {
    why = "fewer checkpoints than were taken";
    return std::nullopt;
  }
  return out;
}

bool write_rehashed(const std::string& path, std::string_view text,
                    const std::vector<std::string>& checkpoints) {
  std::string_view why;
  const std::optional<std::string> out = rehash_text(text, checkpoints, why);
  if (!out) {
    std::fprintf(stderr, "amberfolio: rehash - %.*s\n",
                 static_cast<int>(why.size()), why.data());
    return false;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(out->data(), static_cast<std::streamsize>(out->size()));
  if (!file) {
    std::fprintf(stderr, "amberfolio: rehash - cannot write %s\n",
                 path.c_str());
    return false;
  }
  std::fprintf(stderr, "amberfolio: rehash %s checkpoints=%zu\n", path.c_str(),
               checkpoints.size());
  return true;
}

}  // namespace amberfolio::sdl
