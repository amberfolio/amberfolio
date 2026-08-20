// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio {
namespace {

/// K, the sixty-four round constants: the fractional parts of the cube
/// roots of the first sixty-four primes (FIPS 180-4 §4.2.2).
constexpr std::array<std::uint32_t, 64> round_constants = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu,
    0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u,
    0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u,
    0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu, 0x983E5152u,
    0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
    0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu,
    0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u,
    0xD6990624u, 0xF40E3585u, 0x106AA070u, 0x19A4C116u, 0x1E376C08u,
    0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu,
    0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                   unsigned by) noexcept {
  return (value >> by) | (value << (32u - by));
}

// The six functions FIPS 180-4 §4.1.2 names, spelled out rather than
// folded into the round loop: the round loop is where a transcription
// mistake hides, and each of these is checkable against the standard on
// its own line.

[[nodiscard]] constexpr std::uint32_t choose(std::uint32_t x, std::uint32_t y,
                                             std::uint32_t z) noexcept {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(std::uint32_t x, std::uint32_t y,
                                               std::uint32_t z) noexcept {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma0(std::uint32_t x) noexcept {
  return rotate_right(x, 2) ^ rotate_right(x, 13) ^ rotate_right(x, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma1(std::uint32_t x) noexcept {
  return rotate_right(x, 6) ^ rotate_right(x, 11) ^ rotate_right(x, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma0(std::uint32_t x) noexcept {
  return rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3u);
}

[[nodiscard]] constexpr std::uint32_t small_sigma1(std::uint32_t x) noexcept {
  return rotate_right(x, 17) ^ rotate_right(x, 19) ^ (x >> 10u);
}

}  // namespace

void sha256_hasher::compress(
    std::span<const std::uint8_t, block_bytes> block) noexcept {
  // The message schedule: sixteen big-endian words out of the block, then
  // forty-eight derived from them.
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24u) |
           (static_cast<std::uint32_t>(block[(i * 4) + 1]) << 16u) |
           (static_cast<std::uint32_t>(block[(i * 4) + 2]) << 8u) |
           static_cast<std::uint32_t>(block[(i * 4) + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    w[i] =
        small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t t1 =
        h + big_sigma1(e) + choose(e, f, g) + round_constants[i] + w[i];
    const std::uint32_t t2 = big_sigma0(a) + majority(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void sha256_hasher::update(std::span<const std::uint8_t> data) noexcept {
  length_ += data.size();

  std::size_t taken = 0;

  // Top up whatever is held back first, so that the fast path below only
  // ever sees a hasher standing on a block boundary.
  if (buffered_ != 0) {
    const std::size_t room = block_bytes - buffered_;
    const std::size_t count = (data.size() < room) ? data.size() : room;
    for (std::size_t i = 0; i < count; ++i) {
      buffer_[buffered_ + i] = data[i];
    }
    buffered_ += count;
    taken = count;
    if (buffered_ < block_bytes) {
      return;
    }
    compress(std::span<const std::uint8_t, block_bytes>(buffer_));
    buffered_ = 0;
  }

  while (data.size() - taken >= block_bytes) {
    compress(data.subspan(taken).first<block_bytes>());
    taken += block_bytes;
  }

  for (std::size_t i = taken; i < data.size(); ++i) {
    buffer_[buffered_++] = data[i];
  }
}

sha256_digest sha256_hasher::finish() noexcept {
  // The padding (FIPS 180-4 §5.1.1): one 0x80 byte, then zeroes, then the
  // message length in bits as a big-endian 64-bit value, arranged so that
  // the whole padded message is a multiple of the block size.
  const std::uint64_t bits = length_ * 8u;

  buffer_[buffered_++] = 0x80u;
  if (buffered_ > block_bytes - 8) {
    while (buffered_ < block_bytes) {
      buffer_[buffered_++] = 0;
    }
    compress(std::span<const std::uint8_t, block_bytes>(buffer_));
    buffered_ = 0;
  }
  while (buffered_ < block_bytes - 8) {
    buffer_[buffered_++] = 0;
  }
  for (unsigned i = 0; i < 8; ++i) {
    buffer_[buffered_++] = static_cast<std::uint8_t>(bits >> (56u - (i * 8u)));
  }
  compress(std::span<const std::uint8_t, block_bytes>(buffer_));
  buffered_ = 0;

  sha256_digest digest{};
  for (std::size_t i = 0; i < state_.size(); ++i) {
    digest.bytes[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24u);
    digest.bytes[(i * 4) + 1] = static_cast<std::uint8_t>(state_[i] >> 16u);
    digest.bytes[(i * 4) + 2] = static_cast<std::uint8_t>(state_[i] >> 8u);
    digest.bytes[(i * 4) + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

sha256_digest sha256(std::span<const std::uint8_t> data) noexcept {
  sha256_hasher hasher;
  hasher.update(data);
  return hasher.finish();
}

std::size_t format_hex(const sha256_digest& digest,
                       std::span<char> out) noexcept {
  if (out.size() < sha256_digest::text_length + 1) {
    return 0;
  }
  constexpr std::array<char, 16> digits = {'0', '1', '2', '3', '4', '5',
                                           '6', '7', '8', '9', 'a', 'b',
                                           'c', 'd', 'e', 'f'};
  for (std::size_t i = 0; i < digest.bytes.size(); ++i) {
    out[i * 2] = digits[digest.bytes[i] >> 4u];
    out[(i * 2) + 1] = digits[digest.bytes[i] & 0x0Fu];
  }
  out[sha256_digest::text_length] = '\0';
  return sha256_digest::text_length;
}

}  // namespace amberfolio
