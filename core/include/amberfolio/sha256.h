// SPDX-License-Identifier: AGPL-3.0-only
//
// SHA-256, over bytes, in core.
//
// It is here rather than in a host because of what it is for. PLAN.md §2
// makes the SHA-256 of a player's file the *identity* of that file: the
// boot driver prints it at load (M3-F1, #83), and M4's seam engine keys
// its fingerprint table on it — "opt-in runtime patches ... keyed by
// binary SHA-256 fingerprint" (PLAN.md §5). Two hosts and a seam table
// have to agree about what the fingerprint of a file *is*, and the only
// way to guarantee that is one implementation, below the hosts, hashing
// the same bytes the loader reads.
//
// It is also a fact rather than content: a digest names a file without
// carrying anything out of it, which is precisely why CONTRIBUTING.md
// lists "SHA fingerprints" among the things this project may write down.
//
// The implementation is FIPS 180-4's, written from the standard: eight
// working variables, sixty-four rounds, the sixty-four round constants,
// the padding rule. Nothing here is clever and nothing here is ours to
// invent — a hash that differed from every other SHA-256 in the world
// would be a fingerprint nobody else could check.
//
//
// What it does not do
// -------------------
//
// No allocation, no host calls, nothing that can throw: the same rules
// the rest of core lives by (PLAN.md §4). A hasher is 112 bytes and can
// sit on a stack; a file is fed to it in whatever chunks the caller finds
// convenient, which is what lets `machine/fingerprint.h` hash a file off
// the VFS through a small buffer rather than reading a whole program into
// memory to hash it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio {

/// A SHA-256 result: thirty-two bytes, most significant first, which is
/// the order every published digest is written in.
struct sha256_digest {
  static constexpr std::size_t byte_length = 32;

  /// Characters `format_hex()` writes, the terminator not counted.
  static constexpr std::size_t text_length = byte_length * 2;

  std::array<std::uint8_t, byte_length> bytes{};

  friend constexpr bool operator==(const sha256_digest&,
                                   const sha256_digest&) = default;
};

/// The incremental form: `update()` as many times as there is data,
/// `finish()` once.
///
/// A hasher is finished exactly once. `finish()` applies the padding and
/// hands back the digest; calling `update()` afterwards would be feeding
/// a stream that has already ended, so the object is done and a caller
/// that wants another digest constructs another hasher. (There is no
/// `reset()`: it would exist only to save constructing a 112-byte object,
/// and it would be one more state a caller could get wrong.)
class sha256_hasher {
 public:
  /// Bytes in one compression block — 512 bits, the whole of what the
  /// standard's message schedule is built over. Public because a caller
  /// feeding a file wants a buffer that is a multiple of it.
  static constexpr std::size_t block_bytes = 64;

  sha256_hasher() noexcept = default;

  /// Absorb `data`. Any length, any number of times; the split between
  /// calls is invisible to the result, which is the whole point of an
  /// incremental hash.
  void update(std::span<const std::uint8_t> data) noexcept;

  /// Pad, absorb the length, and produce the digest. Call once.
  [[nodiscard]] sha256_digest finish() noexcept;

 private:
  void compress(std::span<const std::uint8_t, block_bytes> block) noexcept;

  /// H(0), the eight initial hash values: the fractional parts of the
  /// square roots of the first eight primes (FIPS 180-4 §5.3.3).
  std::array<std::uint32_t, 8> state_{0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u,
                                      0xA54FF53Au, 0x510E527Fu, 0x9B05688Cu,
                                      0x1F83D9ABu, 0x5BE0CD19u};

  /// Bytes held back because they do not fill a block yet.
  std::array<std::uint8_t, block_bytes> buffer_{};
  std::size_t buffered_{};

  /// The message length, in bytes, which the padding encodes in bits.
  std::uint64_t length_{};
};

/// The one-shot form, for a caller that already has every byte.
[[nodiscard]] sha256_digest sha256(std::span<const std::uint8_t> data) noexcept;

/// Write `digest` as `sha256_digest::text_length` lowercase hex
/// characters into `out`, NUL-terminated, and answer how many characters
/// that was (the terminator not counted).
///
/// Lowercase because that is how every tool that prints one — `sha256sum`,
/// `Get-FileHash | ForEach-Object ToLower`, GitHub's own release digests —
/// prints one, and a fingerprint a player is meant to be able to compare
/// by eye against another tool's output should not need a case conversion
/// first.
///
/// Zero, and nothing written, if `out` has no room for the text and its
/// terminator: a truncated fingerprint is a different fingerprint, so
/// this refuses rather than shortening (PLAN.md §3's rule, at the one
/// place in this file where there is anything to refuse).
std::size_t format_hex(const sha256_digest& digest,
                       std::span<char> out) noexcept;

}  // namespace amberfolio
