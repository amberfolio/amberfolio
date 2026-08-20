// SPDX-License-Identifier: AGPL-3.0-only
//
// Writes the directory the SDL host's headless smoke tests run against:
// two self-written MZ programs, one that finishes and one that does not.
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
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

/// HELLO.EXE, the M2 check: print a known string, exit with a known code.
///
/// DS points at the PSP and the image is loaded at PSP+10h, so an offset
/// into the image is 0x100 further on in DS terms — which is why the
/// string's address below is its image offset plus 0x100. The string
/// begins immediately after these twelve instruction bytes, at image
/// offset 0x0C, hence 0x010C: an address four bytes past that prints a
/// message missing its first four characters, which is how this was got
/// wrong the first time and how it was caught.
constexpr std::array<std::uint8_t, 12> hello_program = {
    0xBA, 0x0C, 0x01,  // MOV DX, 010Ch   ; DS:DX -> the string
    0xB4, 0x09,        // MOV AH, 09h     ; print $-terminated
    0xCD, 0x21,        // INT 21h
    0xB8, 0x07, 0x4C,  // MOV AX, 4C07h   ; exit, code 7
    0xCD, 0x21,        // INT 21h
};

constexpr const char* hello_text = "amberfolio host says hello\r\n$";

/// STOPPER.EXE, the M3-F1 check (#83): ask the BIOS/DOS layer for a
/// service that is not there, and let the machine refuse.
///
/// INT 63h, and the vector is the point. The service floor lays a stub
/// for all 256 vectors and backs the ones PLAN.md §3 scopes; 63h is
/// backed by nothing, is not part of any interface this emulator will
/// ever provide, and so cannot quietly become implemented and turn this
/// test green for the wrong reason. AH is loaded first only so that the
/// report has a value to name — the refusal happens at the vector, before
/// any handler could look at it.
///
/// There is no exit path after the INT. There does not need to be one:
/// an unbacked vector never returns to its caller, which is the whole
/// shape of "log, don't fake" at this layer (diagnostics.h). The HLT is
/// there so that a run which somehow *did* return has somewhere defined
/// to end up rather than executing whatever follows in memory.
constexpr std::array<std::uint8_t, 5> stopper_program = {
    0xB4, 0x77,  // MOV AH, 77h     ; something for the report to name
    0xCD, 0x63,  // INT 63h         ; nothing backs this vector
    0xF4,        // HLT             ; unreachable; see above
};

/// Wrap `image` in a 32-byte (two-paragraph) MZ header with no
/// relocations and write it to `root/name`. CS:IP is 0000:0000 and SS:SP
/// is 0000:0100, both relative to the load segment the loader picks.
[[nodiscard]] bool write_exe(const std::filesystem::path& root,
                             const char* name,
                             std::span<const std::uint8_t> image) {
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

  std::ofstream out(root / name, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
  return static_cast<bool>(out);
}

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

  std::vector<std::uint8_t> hello(hello_program.begin(), hello_program.end());
  for (const char* c = hello_text; *c != '\0'; ++c) {
    hello.push_back(static_cast<std::uint8_t>(*c));
  }

  if (!write_exe(root, "HELLO.EXE", hello)) {
    std::fprintf(stderr, "cannot write HELLO.EXE\n");
    return EXIT_FAILURE;
  }
  if (!write_exe(root, "STOPPER.EXE", stopper_program)) {
    std::fprintf(stderr, "cannot write STOPPER.EXE\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
} catch (const std::exception& e) {
  // Same reason as the host's own main: this allocates, and an escaping
  // exception would fail the test with no explanation of what happened.
  std::fprintf(stderr, "make_smoke_disk: %s\n", e.what());
  return EXIT_FAILURE;
}
