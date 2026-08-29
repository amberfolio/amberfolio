// SPDX-License-Identifier: AGPL-3.0-only
//
// Write the journal probe document to a file, so a CTest case has one to
// point `--journal` at (M5-E3, #174).
//
// `make_smoke_disk.cpp` and `make_demo_disk.cpp` next door are the same
// idea for the machine's own tests: a test needs an artifact, the
// artifact is generated rather than committed, and the generator is a
// tool of two dozen lines. Here it matters more than usual — the artifact
// is a *document*, and the whole reason this one is generated is that no
// real one may ever be in this tree (`host/journal_probe.h`,
// CONTRIBUTING.md).

#include <cstdio>
#include <exception>
#include <fstream>
#include <vector>

#include "amberfolio/host/journal_probe.h"

int main(int argc, char** argv) try {
  if (argc != 2) {
    std::fprintf(stderr, "usage: amberfolio-sdl-journal-probe <file.pdf>\n");
    return 1;
  }

  const std::vector<unsigned char>& bytes =
      amberfolio::host::journal_probe_pdf();
  std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    std::fprintf(stderr, "amberfolio: could not write %s\n", argv[1]);
    return 1;
  }
  std::fprintf(stderr, "amberfolio: journal probe %s (%zu bytes)\n", argv[1],
               bytes.size());
  return 0;
} catch (const std::exception& e) {
  // Same reason as make_smoke_disk's own main: this allocates, and an
  // escaping exception would fail the test with no explanation.
  std::fprintf(stderr, "make_journal_probe: %s\n", e.what());
  return 1;
}
