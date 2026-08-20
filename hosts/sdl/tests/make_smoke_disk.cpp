// SPDX-License-Identifier: AGPL-3.0-only
//
// Writes the directory the SDL host's M2 smoke test runs against: one
// self-written MZ program that prints a known string and exits with a
// known code.
//
// A generator rather than a committed binary, because a checked-in .EXE
// is exactly the kind of opaque blob the content guard exists to keep out
// (CONTRIBUTING.md) — and rather than a CMake script, because CMake
// cannot write a NUL byte and an MZ header is full of them.
//
// Every byte here is ours. The listing beside each instruction is what
// makes that checkable by eye.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace {

/// The program. DS points at the PSP and the image is loaded at PSP+10h,
/// so an offset into the image is 0x100 further on in DS terms — which is
/// why the string's address below is its image offset plus 0x100. The
/// string begins immediately after these twelve instruction bytes, at
/// image offset 0x0C, hence 0x010C: an address four bytes past that
/// prints a message missing its first four characters, which is how this
/// was got wrong the first time and how it was caught.
constexpr std::array<std::uint8_t, 12> program = {
    0xBA, 0x0C, 0x01,  // MOV DX, 010Ch   ; DS:DX -> the string
    0xB4, 0x09,        // MOV AH, 09h     ; print $-terminated
    0xCD, 0x21,        // INT 21h
    0xB8, 0x07, 0x4C,  // MOV AX, 4C07h   ; exit, code 7
    0xCD, 0x21,        // INT 21h
};

constexpr const char* text = "amberfolio host says hello\r\n$";

}  // namespace

int main(int argc, char** argv) try {
  if (argc != 2) {
    std::fprintf(stderr, "usage: make_smoke_disk <dir>\n");
    return EXIT_FAILURE;
  }

  std::error_code ec;
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s\n", root.string().c_str());
    return EXIT_FAILURE;
  }

  std::vector<std::uint8_t> image(program.begin(), program.end());
  for (const char* c = text; *c != '\0'; ++c) {
    image.push_back(static_cast<std::uint8_t>(*c));
  }

  // A 32-byte (two-paragraph) header with no relocations. CS:IP is
  // 0000:0000 and SS:SP is 0000:0100, both relative to the load segment
  // the loader picks.
  constexpr std::uint16_t header_paragraphs = 2;
  // Sized in the type it is used in, so the products below are not an
  // `unsigned` multiplication widened after the fact — the habit
  // clang-tidy's bugprone-implicit-widening check exists to break.
  constexpr std::size_t header_bytes = std::size_t{header_paragraphs} * 16U;
  const std::size_t total = header_bytes + image.size();
  const auto pages = static_cast<std::uint16_t>((total + 511U) / 512U);
  const auto last_page = static_cast<std::uint16_t>(total % 512U);

  std::vector<std::uint8_t> file(header_bytes, 0);
  const auto put = [&file](std::size_t at, std::uint16_t value) {
    file[at] = static_cast<std::uint8_t>(value & 0xFFU);
    file[at + 1] = static_cast<std::uint8_t>(value >> 8U);
  };
  file[0] = 'M';
  file[1] = 'Z';
  put(2, last_page);
  put(4, pages);
  put(6, 0);  // relocation count
  put(8, header_paragraphs);
  put(10, 0x0010);  // MINALLOC, paragraphs
  put(12, 0xFFFF);  // MAXALLOC
  put(14, 0x0000);  // initial SS
  put(16, 0x0100);  // initial SP
  put(18, 0x0000);  // checksum
  put(20, 0x0000);  // initial IP
  put(22, 0x0000);  // initial CS
  put(24, 0x001C);  // relocation table offset

  file.insert(file.end(), image.begin(), image.end());

  std::ofstream out(root / "HELLO.EXE", std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
  if (!out) {
    std::fprintf(stderr, "cannot write HELLO.EXE\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
} catch (const std::exception& e) {
  // Same reason as the host's own main: this allocates, and an escaping
  // exception would fail the test with no explanation of what happened.
  std::fprintf(stderr, "make_smoke_disk: %s\n", e.what());
  return EXIT_FAILURE;
}
