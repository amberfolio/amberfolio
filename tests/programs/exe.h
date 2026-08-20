// SPDX-License-Identifier: AGPL-3.0-only
//
// The MZ container: the twenty-eight fixed header bytes, the relocation
// table, and the image after them.
//
// Its own pair of files rather than a lump inside machine_programs.cpp,
// because there is now a second caller. The SDL host's demo disk
// (hosts/sdl/tests/make_demo_disk.cpp) stages programs a person runs in a
// window, and a program the host can load is a program with one of these
// on the front of it.
//
// It is still a second copy of what tests/core/machine/loader_test.cpp
// builds, and still deliberately so: that one lives inside the GoogleTest
// rig, which does not build under Emscripten, and this directory may not
// depend on it (machine_harness.h). What that argument was never about
// was having two copies *here*.

#pragma once

#include <cstdint>
#include <vector>

namespace amberfolio::programs {

struct reloc_entry {
  std::uint16_t offset{};
  std::uint16_t segment{};
};

struct exe_spec {
  std::uint16_t initial_cs{};
  std::uint16_t initial_ip{};
  std::uint16_t initial_ss{};
  std::uint16_t initial_sp{};
  std::uint16_t min_alloc{};
  std::vector<reloc_entry> relocations;
  std::vector<std::uint8_t> image;
};

/// A complete, well-formed MZ file: the fixed header, the relocation
/// table immediately after it, and the image after that, with every
/// length field computed from the pieces actually given - the last-page
/// rule included.
[[nodiscard]] std::vector<std::uint8_t> build_exe(const exe_spec& spec);

}  // namespace amberfolio::programs
