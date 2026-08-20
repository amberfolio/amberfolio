// SPDX-License-Identifier: AGPL-3.0-only

#include "programs/exe.h"

#include <cstddef>

#include "amberfolio/machine/loader.h"

namespace amberfolio::programs {
namespace {

void put16(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint16_t value) {
  bytes[at] = static_cast<std::uint8_t>(value);
  bytes[at + 1] = static_cast<std::uint8_t>(value >> 8U);
}

}  // namespace

/// A complete, well-formed MZ file: the fixed header, the relocation
/// table immediately after it, and the image after that, with every
/// length field computed from the pieces actually given — the last-page
/// rule included.
[[nodiscard]] std::vector<std::uint8_t> build_exe(const exe_spec& spec) {
  constexpr std::uint16_t table_offset = machine::mz::encoded_size;

  const auto reloc_count = static_cast<std::uint16_t>(spec.relocations.size());
  const std::uint32_t table_bytes = static_cast<std::uint32_t>(reloc_count) *
                                    machine::mz::relocation_entry_size;
  const auto header_paragraphs = static_cast<std::uint16_t>(
      (table_offset + table_bytes + machine::paragraph_size - 1) /
      machine::paragraph_size);
  const std::uint32_t header_size =
      static_cast<std::uint32_t>(header_paragraphs) * machine::paragraph_size;

  const auto total =
      static_cast<std::uint32_t>(header_size + spec.image.size());
  const auto last_page =
      static_cast<std::uint16_t>(total % machine::mz::page_size);
  const auto pages = static_cast<std::uint16_t>(total / machine::mz::page_size +
                                                (last_page != 0 ? 1 : 0));

  std::vector<std::uint8_t> file(header_size + spec.image.size(), 0);
  file[0] = 'M';
  file[1] = 'Z';
  put16(file, 0x02, last_page);
  put16(file, 0x04, pages);
  put16(file, 0x06, reloc_count);
  put16(file, 0x08, header_paragraphs);
  put16(file, 0x0A, spec.min_alloc);
  put16(file, 0x0C, 0xFFFF);  // MAXALLOC: never consulted (loader.h).
  put16(file, 0x0E, spec.initial_ss);
  put16(file, 0x10, spec.initial_sp);
  put16(file, 0x12, 0);  // checksum: nothing reads it.
  put16(file, 0x14, spec.initial_ip);
  put16(file, 0x16, spec.initial_cs);
  put16(file, 0x18, table_offset);
  put16(file, 0x1A, 0);  // overlay number.

  for (std::size_t i = 0; i < spec.relocations.size(); ++i) {
    const std::size_t at =
        table_offset + i * machine::mz::relocation_entry_size;
    put16(file, at, spec.relocations[i].offset);
    put16(file, at + 2, spec.relocations[i].segment);
  }
  for (std::size_t i = 0; i < spec.image.size(); ++i) {
    file[header_size + i] = spec.image[i];
  }
  return file;
}

}  // namespace amberfolio::programs
