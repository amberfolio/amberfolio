// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/service_floor.h"

namespace amberfolio::machine {
namespace {

/// The byte offsets of the fixed header's fields, private to this file:
/// `mz_header` names them, `loader.h` does not need to.
namespace field {

inline constexpr std::size_t last_page_size = 0x02;
inline constexpr std::size_t page_count = 0x04;
inline constexpr std::size_t relocation_count = 0x06;
inline constexpr std::size_t header_paragraphs = 0x08;
inline constexpr std::size_t min_alloc = 0x0A;
inline constexpr std::size_t max_alloc = 0x0C;
inline constexpr std::size_t initial_ss = 0x0E;
inline constexpr std::size_t initial_sp = 0x10;
inline constexpr std::size_t checksum = 0x12;
inline constexpr std::size_t initial_ip = 0x14;
inline constexpr std::size_t initial_cs = 0x16;
inline constexpr std::size_t relocation_table_offset = 0x18;
inline constexpr std::size_t overlay_number = 0x1A;

}  // namespace field

/// DOS's own vector for the old-style terminate. Not in `service_floor.h`
/// because nothing there needs to name it — only this file installs a
/// handler for it, and only because this loader is what makes the PSP
/// that contains one exist at all.
inline constexpr std::uint8_t terminate_vector = 0x20;

[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::uint8_t> raw,
                                        std::size_t at) noexcept {
  const auto lo = static_cast<unsigned>(raw[at]);
  const auto hi = static_cast<unsigned>(raw[at + 1]);
  return static_cast<std::uint16_t>(lo | (hi << 8u));
}

/// Read exactly `out.size()` bytes from `handle`, looping over whatever
/// short reads `vfs.h`'s contract allows a backend to give. Every length
/// this loader ever reads with has already been checked against
/// `fs.stat()`'s own answer before this is called, so a short count here
/// means the file changed under us or the backend is misbehaving —
/// either way `loader_error::file_error` is the honest answer, not a
/// silent partial load.
[[nodiscard]] loader_error read_exact(filesystem& fs, file_handle handle,
                                      std::span<std::uint8_t> out) {
  std::size_t done = 0;
  while (done < out.size()) {
    const auto got = fs.read(handle, out.subspan(done));
    if (!got.ok() || got.value == 0) {
      return loader_error::file_error;
    }
    done += got.value;
  }
  return loader_error::none;
}

/// Patch one relocation entry's word by `image_load_segment` — this
/// loader's whole reason to exist, and see this file's header comment
/// for why the two physical addresses below are computed the way they
/// are and not as `addr` and `addr + 1`.
[[nodiscard]] loader_error apply_relocation(machine& box,
                                            std::uint16_t entry_segment,
                                            std::uint16_t entry_offset) {
  const auto target_segment =
      static_cast<std::uint16_t>(image_load_segment + entry_segment);

  const std::uint32_t low_addr =
      cpu::physical_address(target_segment, entry_offset);
  // The offset of the word's high byte, formed by a 16-bit addition that
  // wraps on its own when `entry_offset` is 0xFFFF — the high byte is
  // then offset 0x0000 of the *same* segment, not the paragraph
  // physically after it.
  const std::uint32_t high_addr = cpu::physical_address(
      target_segment, static_cast<std::uint16_t>(entry_offset + 1));

  // The memory the program was granted, not the narrower loaded-image
  // range — this file's top comment on why.
  const std::uint32_t granted_base = cpu::physical_address(psp_load_segment, 0);
  const auto within_granted_memory = [&](std::uint32_t addr) noexcept {
    return addr >= granted_base && addr < conventional_ram_size;
  };
  if (!within_granted_memory(low_addr) || !within_granted_memory(high_addr)) {
    return loader_error::relocation_out_of_image;
  }

  const std::span<std::uint8_t> ram = box.memory().ram();
  const auto word =
      static_cast<std::uint16_t>(static_cast<unsigned>(ram[low_addr]) |
                                 (static_cast<unsigned>(ram[high_addr]) << 8u));
  const auto relocated = static_cast<std::uint16_t>(word + image_load_segment);
  ram[low_addr] = static_cast<std::uint8_t>(relocated);
  ram[high_addr] = static_cast<std::uint8_t>(relocated >> 8u);
  return loader_error::none;
}

/// Write the PSP's honestly-fillable fields at `psp_load_segment` — see
/// loader.h's top comment for which fields those are and why the rest
/// stay zero.
void write_psp(machine& box, std::span<const char> command_tail) {
  const std::uint32_t base = cpu::physical_address(psp_load_segment, 0);
  const std::span<std::uint8_t> block =
      box.memory().ram().subspan(base, psp::size);
  for (std::uint8_t& byte : block) {
    byte = 0;
  }

  block[psp::int20_offset] = 0xCD;
  block[psp::int20_offset + 1] = 0x20;

  // Top of memory: one past the last paragraph this program owns — all
  // remaining conventional memory, the MAXALLOC-style default this
  // file's top comment explains. Fixed at the top of conventional RAM
  // rather than derived from the image, because placement always grants
  // everything above it.
  const auto top_of_memory =
      static_cast<std::uint16_t>(conventional_ram_size / paragraph_size);
  block[psp::top_of_memory_offset] = static_cast<std::uint8_t>(top_of_memory);
  block[psp::top_of_memory_offset + 1] =
      static_cast<std::uint8_t>(top_of_memory >> 8u);

  // Parent (0x16) and environment (0x2C) segments are left at the zero
  // the clear above already put there: the documented placeholders.

  const auto count = static_cast<std::uint8_t>(command_tail.size());
  block[psp::command_tail_count_offset] = count;
  for (std::size_t i = 0; i < command_tail.size(); ++i) {
    block[psp::command_tail_bytes_offset + i] =
        static_cast<std::uint8_t>(command_tail[i]);
  }
  block[psp::command_tail_bytes_offset + command_tail.size()] = 0x0D;
}

/// The PSP's own INT 20h. DOS's convention for the old-style terminate
/// carries no code, so it is always 0 — AH=4Ch (#52) is how a program
/// reports a real one, through the same `machine::exit_program()`.
void handle_int20(service_floor& floor, std::uint8_t /*vector*/) {
  floor.box().exit_program(0);
}

}  // namespace

loader_result<mz_header> mz_header::decode(
    std::span<const std::uint8_t> raw) noexcept {
  if (raw.size() < mz::encoded_size || read_u16_le(raw, 0) != mz::signature) {
    return {.error = loader_error::bad_signature};
  }

  mz_header header{};
  header.last_page_size = read_u16_le(raw, field::last_page_size);
  header.page_count = read_u16_le(raw, field::page_count);
  header.relocation_count = read_u16_le(raw, field::relocation_count);
  header.header_paragraphs = read_u16_le(raw, field::header_paragraphs);
  header.min_alloc = read_u16_le(raw, field::min_alloc);
  header.max_alloc = read_u16_le(raw, field::max_alloc);
  header.initial_ss = read_u16_le(raw, field::initial_ss);
  header.initial_sp = read_u16_le(raw, field::initial_sp);
  header.checksum = read_u16_le(raw, field::checksum);
  header.initial_ip = read_u16_le(raw, field::initial_ip);
  header.initial_cs = read_u16_le(raw, field::initial_cs);
  header.relocation_table_offset =
      read_u16_le(raw, field::relocation_table_offset);
  header.overlay_number = read_u16_le(raw, field::overlay_number);
  return {.value = header};
}

loader_result<std::uint32_t> mz_header::image_length() const noexcept {
  if (page_count == 0 || last_page_size > mz::page_size) {
    return {.error = loader_error::bad_image_length};
  }

  // The last-page-size rule: 0 means the last page is full, not empty.
  const std::uint32_t length =
      (last_page_size == 0)
          ? static_cast<std::uint32_t>(page_count) * mz::page_size
          : static_cast<std::uint32_t>(page_count - 1) * mz::page_size +
                last_page_size;
  return {.value = length};
}

loader_result<loaded_program> load_program(machine& box, filesystem& fs,
                                           const dos_path& path,
                                           std::span<const char> command_tail) {
  if (command_tail.size() > psp::command_tail_max_length) {
    return {.error = loader_error::command_tail_too_long};
  }

  // Every length below is checked against this one fact before it is
  // used for anything — loader.h's top comment on why.
  const auto stat = fs.stat(path);
  if (!stat.ok() || stat.value.is_directory) {
    return {.error = loader_error::file_error};
  }
  const std::uint32_t file_size = stat.value.size;
  if (file_size < mz::encoded_size) {
    return {.error = loader_error::bad_signature};
  }

  const auto opened = fs.open(path, open_mode::read_only);
  if (!opened.ok()) {
    return {.error = loader_error::file_error};
  }
  const file_handle handle = opened.value;

  std::array<std::uint8_t, mz::encoded_size> header_bytes{};
  if (const loader_error err = read_exact(fs, handle, header_bytes);
      err != loader_error::none) {
    fs.close(handle);
    return {.error = err};
  }

  const loader_result<mz_header> decoded = mz_header::decode(header_bytes);
  if (!decoded.ok()) {
    fs.close(handle);
    return {.error = decoded.error};
  }
  const mz_header& header = decoded.value;

  const loader_result<std::uint32_t> image_len = header.image_length();
  if (!image_len.ok() || image_len.value > file_size) {
    fs.close(handle);
    return {.error = loader_error::bad_image_length};
  }

  const std::uint32_t header_size = header.header_size();
  if (header_size < mz::encoded_size || header_size > image_len.value) {
    fs.close(handle);
    return {.error = loader_error::bad_header_size};
  }

  const std::uint32_t reloc_table_end =
      static_cast<std::uint32_t>(header.relocation_table_offset) +
      static_cast<std::uint32_t>(header.relocation_count) *
          mz::relocation_entry_size;
  if (reloc_table_end > header_size) {
    fs.close(handle);
    return {.error = loader_error::bad_relocation_table};
  }

  const std::uint32_t load_module_size = image_len.value - header_size;
  const std::uint32_t image_base = cpu::physical_address(image_load_segment, 0);
  if (load_module_size > conventional_ram_size ||
      image_base + load_module_size > conventional_ram_size) {
    fs.close(handle);
    return {.error = loader_error::image_too_large};
  }

  // MINALLOC: what is left of conventional memory above the image must
  // cover the program's own stated minimum. MAXALLOC is never consulted
  // — loader.h's top comment on why.
  const std::uint32_t available_paragraphs =
      (conventional_ram_size - image_base - load_module_size) / paragraph_size;
  if (available_paragraphs < header.min_alloc) {
    fs.close(handle);
    return {.error = loader_error::insufficient_memory};
  }

  // The entry state, relocated and validated before anything is written
  // to memory: a failure past this point must still mean nothing above
  // has happened (this file's header comment on `load_program()`), and
  // both of these depend only on the header and the fixed placement
  // constants, never on the bytes still to be read.
  const auto entry_cs =
      static_cast<std::uint16_t>(header.initial_cs + image_load_segment);
  const auto entry_ss =
      static_cast<std::uint16_t>(header.initial_ss + image_load_segment);
  const std::uint16_t entry_ip = header.initial_ip;
  const std::uint16_t entry_sp = header.initial_sp;

  if (cpu::physical_address(entry_cs, entry_ip) >= conventional_ram_size ||
      cpu::physical_address(entry_ss, 0) >= conventional_ram_size) {
    fs.close(handle);
    return {.error = loader_error::bad_entry_point};
  }

  // The load module, straight into RAM: the machine writing memory, not
  // the program (memory_map.h's back door).
  {
    const auto sought = fs.seek(handle, seek_origin::begin,
                                static_cast<std::int32_t>(header_size));
    if (!sought.ok()) {
      fs.close(handle);
      return {.error = loader_error::file_error};
    }
    const std::span<std::uint8_t> dest =
        box.memory().ram().subspan(image_base, load_module_size);
    if (const loader_error err = read_exact(fs, handle, dest);
        err != loader_error::none) {
      fs.close(handle);
      return {.error = err};
    }
  }

  // Relocations: this loader's whole reason to exist.
  {
    const auto sought =
        fs.seek(handle, seek_origin::begin,
                static_cast<std::int32_t>(header.relocation_table_offset));
    if (!sought.ok()) {
      fs.close(handle);
      return {.error = loader_error::file_error};
    }

    for (std::uint16_t i = 0; i < header.relocation_count; ++i) {
      std::array<std::uint8_t, mz::relocation_entry_size> entry{};
      if (const loader_error err = read_exact(fs, handle, entry);
          err != loader_error::none) {
        fs.close(handle);
        return {.error = err};
      }

      const loader_error applied =
          apply_relocation(box, /*entry_segment=*/read_u16_le(entry, 2),
                           /*entry_offset=*/read_u16_le(entry, 0));
      if (applied != loader_error::none) {
        fs.close(handle);
        return {.error = applied};
      }
    }
  }

  fs.close(handle);

  write_psp(box, command_tail);

  // The PSP's own terminate path — see loader.h's top comment on why the
  // exit path is wired here rather than left for #52.
  box.services().provide(terminate_vector, &handle_int20);

  // Entry state: DS=ES=PSP segment, CS:IP/SS:SP relocated, AX=0000h (the
  // FCB drive-validity convention), flags clean. `reset()` first, so
  // every register this loader does not name is the zero real DOS never
  // documents but this machine can honestly give.
  cpu::processor& cpu_proc = box.processor();
  cpu_proc.reset();
  cpu::registers& regs = cpu_proc.regs();
  regs[cpu::sreg::ds] = psp_load_segment;
  regs[cpu::sreg::es] = psp_load_segment;
  regs[cpu::sreg::cs] = entry_cs;
  regs.ip = entry_ip;
  regs[cpu::sreg::ss] = entry_ss;
  regs[cpu::reg16::sp] = entry_sp;
  regs[cpu::reg16::ax] = 0x0000;

  return {.value = loaded_program{.psp_segment = psp_load_segment,
                                  .load_segment = image_load_segment,
                                  .entry_cs = entry_cs,
                                  .entry_ip = entry_ip,
                                  .entry_ss = entry_ss,
                                  .entry_sp = entry_sp}};
}

}  // namespace amberfolio::machine
