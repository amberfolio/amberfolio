// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's page decoder. leptonica_decoder.h has the reasoning.

#include "leptonica_decoder.h"

#include <leptonica/allheaders.h>

#include <cstdint>
#include <span>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::sdl {

bool leptonica_page_decoder::decode(std::span<const std::uint8_t> stream,
                                    host::journal_bitmap& out) {
  out = host::journal_bitmap{};
  if (stream.empty()) {
    return false;
  }
  Pix* page = pixReadMem(stream.data(), stream.size());
  if (page == nullptr) {
    return false;
  }
  // Eight bits of gray, whatever it arrived as. `pixConvertTo8` is a
  // no-op for a page that already is one.
  Pix* gray = pixConvertTo8(page, 0);
  pixDestroy(&page);
  if (gray == nullptr) {
    return false;
  }

  const std::uint32_t width = static_cast<std::uint32_t>(pixGetWidth(gray));
  const std::uint32_t height = static_cast<std::uint32_t>(pixGetHeight(gray));
  if (width == 0 || height == 0) {
    pixDestroy(&gray);
    return false;
  }
  out.width = width;
  out.height = height;
  out.pixels.resize(static_cast<std::size_t>(width) * height);
  // Row by row through Leptonica's own accessor rather than over its
  // packed words: the word order is the library's business and reading
  // it directly would be this file knowing something it has no reason
  // to.
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      l_uint32 value = 0;
      if (pixGetPixel(gray, static_cast<l_int32>(x), static_cast<l_int32>(y),
                      &value) != 0) {
        pixDestroy(&gray);
        out = host::journal_bitmap{};
        return false;
      }
      out.pixels[(static_cast<std::size_t>(y) * width) + x] =
          static_cast<std::uint8_t>(value & 0xFFU);
    }
  }
  pixDestroy(&gray);
  return true;
}

const char* leptonica_page_decoder::name() const noexcept {
  return "leptonica (linked)";
}

}  // namespace amberfolio::sdl
