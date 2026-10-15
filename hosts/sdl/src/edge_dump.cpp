// SPDX-License-Identifier: AGPL-3.0-only

#include "edge_dump.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <string>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {

void edge_dump::open(const options& opts, machine::machine& box) {
  if (!opts.dump_prefix.empty()) {
    const std::string path = opts.dump_prefix + ".edges";
    file_.open(path, std::ios::trunc);
    if (!file_) {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   path.c_str());
    } else {
      // A header a reader can act on. A tick is meaningless without the
      // rate it is counted at, and this is a file somebody will open in a
      // year with none of this in their head.
      file_ << "# amberfolio audio edges\n"
            << "# pit-input-hz " << machine::pit_input_hz << "\n"
            << "# tick level\n";
      box.audio().log_edges(true);
    }
  }
}

void edge_dump::drain(machine::machine& box) {
  if (!file_.is_open()) {
    return;
  }
  std::array<machine::audio_edge, 256> batch{};
  for (;;) {
    const std::size_t got = box.audio().read_edge_log(batch);
    if (got == 0) {
      return;
    }
    for (std::size_t i = 0; i < got; ++i) {
      file_ << batch[i].at << ' ' << (batch[i].level ? '1' : '0') << '\n';
    }
    written_ += got;
  }
}

void edge_dump::finish(const machine::machine& box, const std::string& prefix) {
  if (!file_.is_open()) {
    return;
  }
  const std::uint64_t lost = box.audio().edge_log_dropped();
  file_ << "# edges " << written_ << " dropped " << lost << '\n';
  file_.close();
  std::fprintf(stderr,
               "amberfolio: dump edges=%s.edges count=%llu dropped=%llu\n",
               prefix.c_str(), static_cast<unsigned long long>(written_),
               static_cast<unsigned long long>(lost));
}

}  // namespace amberfolio::sdl
