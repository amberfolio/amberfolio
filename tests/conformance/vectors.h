// SPDX-License-Identifier: AGPL-3.0-only
//
// One condensed vector file, in memory (issue #14).
//
// The oracle is SingleStepTests/8088 — JSON captured from real 8088
// silicon, pinned to a commit and fetched into a cache outside the source
// tree by scripts/fetch-conformance-vectors.py, which also strips the
// per-cycle bus trace this emulator has no use for. Nothing here fetches
// or condenses; this is the reader for what that script leaves behind.
//
// A vector is a before state, an after state, and the memory and port
// traffic in between. The "after" here is *not* what the file holds: the
// suite records only what changed, and this reader folds those changes
// onto the before state at load time so that a comparison is one
// operation on a whole register file rather than a search for which
// fields were mentioned. `ram_after` stays sparse, because memory is too
// big to fold and the sparseness is the point.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::conformance {

/// One byte of memory at a physical address, the way the vectors give it.
struct memory_byte {
  std::uint32_t address{};
  std::uint8_t value{};
};

/// One port transaction, recovered from the cycle trace by the condenser.
///
/// This is the one thing the trace was needed for: what an IN instruction
/// reads back exists nowhere else in the file, so a harness that dropped
/// the trace outright could not run E4/E5/EC/ED at all.
struct port_op {
  enum class kind : std::uint8_t { read, write };

  kind what{};
  std::uint16_t port{};
  std::uint8_t value{};
};

struct vector_test {
  /// The suite's disassembly of the instruction, e.g. "in al, 1Bh".
  std::string name;
  /// Its index within the file — what the suite calls a test by.
  std::uint32_t idx{};
  /// The encoded instruction, prefixes included.
  std::vector<std::uint8_t> bytes;

  cpu::registers before{};
  /// `before` with the file's recorded changes folded in.
  cpu::registers after{};

  /// Every byte the real part read, plus the instruction itself. A read
  /// outside this set is a failure: the vector would have listed it.
  std::vector<memory_byte> ram_before;
  /// Only the bytes that changed.
  std::vector<memory_byte> ram_after;

  /// Port transactions, in order. Empty for all but the eight port
  /// opcodes.
  std::vector<port_op> ports;
};

struct vector_file {
  std::string stem;
  std::vector<vector_test> tests;
};

/// Where condensed vectors live: $AMBERFOLIO_CONFORMANCE_VECTORS if it is
/// set, and otherwise the per-user cache directory the fetch script
/// writes to. The two computations are deliberately identical — see the
/// note in scripts/fetch-conformance-vectors.py.
[[nodiscard]] std::filesystem::path vector_cache_dir();

/// The condensed file for one stem ("00", "80.0", ...), whether or not it
/// exists.
[[nodiscard]] std::filesystem::path vector_path(std::string_view stem);

/// How many tests per file to run: $AMBERFOLIO_CONFORMANCE_LIMIT, or 0
/// for all of them. The CI jobs that are not the full-suite one set it,
/// and so does anyone who wants a quick answer.
[[nodiscard]] std::size_t test_limit();

/// Read and parse one condensed file, at most `limit` tests (0 = all).
/// Throws std::runtime_error, with a message worth reading, if the file
/// is missing, truncated, compressed wrong, or not what this reader's
/// condenser version produces.
[[nodiscard]] vector_file load_vectors(std::string_view stem,
                                       std::size_t limit);

}  // namespace amberfolio::conformance
