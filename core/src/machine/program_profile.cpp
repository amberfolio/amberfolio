// SPDX-License-Identifier: AGPL-3.0-only
//
// The program profiles (program_profile.h): the list, and the lookup by a
// program image's digest.

#include "amberfolio/machine/program_profile.h"

#include <array>
#include <span>

#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {
namespace {

constexpr std::array<const program_profile*, 2> profiles{
    &pool_of_radiance, &curse_of_the_azure_bonds};

}  // namespace

std::span<const program_profile* const> program_profiles() noexcept {
  return profiles;
}

const program_profile* find_program_profile(
    const sha256_digest& digest) noexcept {
  for (const program_profile* known : profiles) {
    if (digest_is(digest, known->fingerprint)) {
      return known;
    }
  }
  return nullptr;
}

}  // namespace amberfolio::machine
