// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/fingerprint.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {
namespace {

/// Bytes read per `read()` call. Sixty-four blocks of the hash's own
/// block size — big enough that a 200 KiB overlay is fifty calls rather
/// than three thousand, small enough to sit on any stack this code can
/// be called from.
constexpr std::size_t chunk_bytes = sha256_hasher::block_bytes * 64;

}  // namespace

vfs_result<sha256_digest> fingerprint_file(filesystem& fs,
                                           const dos_path& path) {
  const vfs_result<file_handle> opened = fs.open(path, open_mode::read_only);
  if (!opened.ok()) {
    return {.error = opened.error};
  }

  sha256_hasher hasher;
  std::array<std::uint8_t, chunk_bytes> buffer{};

  for (;;) {
    const vfs_result<std::size_t> got = fs.read(opened.value, buffer);
    if (!got.ok()) {
      // Closed before answering, so a failed fingerprint does not leave a
      // handle behind — this backend has sixteen of them (vfs.h) and a
      // boot driver that fingerprinted a few unreadable files would
      // otherwise run out for a reason nothing explains.
      static_cast<void>(fs.close(opened.value));
      return {.error = got.error};
    }
    if (got.value == 0) {
      break;
    }
    hasher.update(std::span<const std::uint8_t>(buffer.data(), got.value));
  }

  const vfs_error closed = fs.close(opened.value);
  if (closed != vfs_error::none) {
    return {.error = closed};
  }
  return {.value = hasher.finish(), .error = vfs_error::none};
}

}  // namespace amberfolio::machine
