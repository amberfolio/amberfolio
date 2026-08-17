// SPDX-License-Identifier: AGPL-3.0-only
//
// The C ABI implementation. It is a translation of the C++ API and holds
// no logic of its own — that is the deal: the boundary stays thin enough
// that nothing can be true on one side of it and false on the other.

#include "amberfolio/abi.h"

#include "amberfolio/version.h"

namespace {

// The packing in abi.h gives each component 8 bits. Nothing enforces that
// on the project version, so state it here rather than silently truncating
// at 0.0.256.
constexpr bool fits_in_a_byte(int n) noexcept { return n >= 0 && n <= 0xFF; }

static_assert(fits_in_a_byte(amberfolio::core_version.major) &&
                  fits_in_a_byte(amberfolio::core_version.minor) &&
                  fits_in_a_byte(amberfolio::core_version.patch),
              "af_version() packs each version component into 8 bits; this "
              "project version does not fit. Widen the packing in abi.h.");

constexpr uint32_t byte_of(int n) noexcept {
  return static_cast<uint32_t>(n) & 0xFFu;
}

}  // namespace

extern "C" uint32_t af_version(void) {
  const amberfolio::version v = amberfolio::linked_version();
  return (byte_of(v.major) << 16) | (byte_of(v.minor) << 8) | byte_of(v.patch);
}
