// SPDX-License-Identifier: AGPL-3.0-only
//
// The page decoder every build has. journal_jpeg.h has the reasoning;
// stb_image_impl.cpp is where the library it calls is compiled.

#include "amberfolio/host/journal_jpeg.h"

#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::host {
namespace {

/// What a decoded page may run to, in samples.
///
/// A bound rather than trust, the same one `journal_store.h`'s caps are:
/// the bytes handed over are a stream out of a document this project did
/// not write, and a header claiming a forty-thousand-square page would
/// otherwise be an allocation nobody asked for. The tabled edition's
/// spreads are 1328 by 1003, and a scan four times that in each direction
/// is still inside this.
constexpr std::size_t max_page_samples = std::size_t{64} * 1024U * 1024U;

}  // namespace

bool jpeg_page_decoder::decode(std::span<const std::uint8_t> stream,
                               journal_bitmap& out) {
  out = journal_bitmap{};
  if (stream.empty()) {
    return false;
  }
  // `int` because that is the library's own type for a length, and a
  // stream longer than one can hold is not a page this reads.
  if (stream.size() > static_cast<std::size_t>(INT32_MAX)) {
    return false;
  }

  int width = 0;
  int height = 0;
  int had = 0;
  // Three components asked for, whatever the page is: a gray JPEG comes
  // back as three equal ones and the luma below is then an identity, and
  // an RGB one comes back as itself. The alternative — one component,
  // stb's own gray — is the one thing this must not do (journal_jpeg.h).
  stbi_uc* pixels = stbi_load_from_memory(
      stream.data(), static_cast<int>(stream.size()), &width, &height, &had, 3);
  if (pixels == nullptr) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    stbi_image_free(pixels);
    return false;
  }

  const auto wide = static_cast<std::size_t>(width);
  const auto tall = static_cast<std::size_t>(height);
  if (wide > max_page_samples / tall) {
    stbi_image_free(pixels);
    return false;
  }

  out.width = static_cast<std::uint32_t>(wide);
  out.height = static_cast<std::uint32_t>(tall);
  out.pixels.resize(wide * tall);
  for (std::size_t at = 0; at < out.pixels.size(); ++at) {
    const std::size_t rgb = at * 3U;
    out.pixels[at] =
        journal_gray(pixels[rgb], pixels[rgb + 1U], pixels[rgb + 2U]);
  }
  stbi_image_free(pixels);
  return true;
}

const char* jpeg_page_decoder::name() const noexcept { return "stb_image"; }

journal_page_decoder& default_page_decoder() noexcept {
  static jpeg_page_decoder one;
  return one;
}

}  // namespace amberfolio::host
