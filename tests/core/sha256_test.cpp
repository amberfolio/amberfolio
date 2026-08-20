// SPDX-License-Identifier: AGPL-3.0-only
//
// SHA-256, against the digests everyone else gets.
//
// A hash implementation is the one kind of code whose correctness cannot
// be argued from its own source: it either agrees with every other
// SHA-256 in the world or it is not SHA-256, and the only way to find out
// is to put known messages through it. The vectors below are the ones
// FIPS 180-4's appendix B publishes and the two every implementation is
// first tried against — public facts about a public standard, no more
// content than a multiplication table (CONTRIBUTING.md).
//
// The cases past them are about *this* implementation rather than about
// the algorithm: the block boundary, the padding boundary, and the
// promise that where a caller splits a stream cannot change the answer.

#include "amberfolio/sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

namespace amberfolio {
namespace {

[[nodiscard]] std::span<const std::uint8_t> bytes_of(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

[[nodiscard]] std::string_view hex_of(const sha256_digest& digest,
                                      std::span<char> scratch) {
  const std::size_t n = format_hex(digest, scratch);
  return {scratch.data(), n};
}

/// A buffer big enough for `format_hex`, so that every case below can ask
/// for the text without repeating the size.
using hex_buffer = std::array<char, sha256_digest::text_length + 1>;

TEST(Sha256, HashesTheEmptyMessage) {
  hex_buffer text{};
  EXPECT_EQ(hex_of(sha256({}), text),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, HashesTheStandardsOwnFirstVector) {
  // FIPS 180-4 appendix B.1: one block, one round of padding.
  hex_buffer text{};
  EXPECT_EQ(hex_of(sha256(bytes_of("abc")), text),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, HashesTheStandardsOwnSecondVector) {
  // Appendix B.2: 56 bytes, which is exactly the length at which the
  // padding no longer fits in the last block and a second one is needed.
  // The case most likely to be wrong in an implementation that works.
  hex_buffer text{};
  EXPECT_EQ(hex_of(sha256(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmk"
                                   "lmnlmnomnopnopq")),
                   text),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, HashesAMillionLetters) {
  // Appendix B.3, the long one: 1,000,000 'a'. It is here because it is
  // the only published vector that exercises the block loop thousands of
  // times over, which is where an off-by-one in the buffering would show.
  const std::vector<std::uint8_t> million(1000000, std::uint8_t{'a'});
  hex_buffer text{};
  EXPECT_EQ(hex_of(sha256(million), text),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, IsUnaffectedByHowTheStreamIsSplit) {
  // The property the incremental form exists to have, checked at every
  // split of a message that straddles two blocks — including the two
  // splits that land exactly on the block boundary, which is the one the
  // buffering code has a branch for.
  std::vector<std::uint8_t> message(sha256_hasher::block_bytes * 2 + 7);
  for (std::size_t i = 0; i < message.size(); ++i) {
    message[i] = static_cast<std::uint8_t>(i * 31u);
  }
  const sha256_digest whole = sha256(message);

  for (std::size_t split = 0; split <= message.size(); ++split) {
    sha256_hasher hasher;
    hasher.update(std::span<const std::uint8_t>(message).first(split));
    hasher.update(std::span<const std::uint8_t>(message).subspan(split));
    EXPECT_EQ(hasher.finish(), whole) << "split at " << split;
  }
}

TEST(Sha256, DiffersOnASingleChangedBit) {
  // Not a property of the standard so much as a check that this
  // implementation is actually reading the message: a hash that ignored
  // its input would pass every fixed vector above by accident of the
  // constants and fail this.
  EXPECT_NE(sha256(bytes_of("abc")), sha256(bytes_of("abd")));
}

TEST(FormatHex, RefusesABufferThatWouldTruncate) {
  // A shortened fingerprint is a different fingerprint, so there is
  // nothing useful a short buffer could be given.
  const sha256_digest digest = sha256({});
  std::array<char, sha256_digest::text_length> too_small{};
  EXPECT_EQ(format_hex(digest, too_small), 0u);
}

TEST(FormatHex, TerminatesWhatItWrites) {
  hex_buffer text{};
  EXPECT_EQ(format_hex(sha256({}), text), sha256_digest::text_length);
  EXPECT_EQ(text[sha256_digest::text_length], '\0');
}

}  // namespace
}  // namespace amberfolio
