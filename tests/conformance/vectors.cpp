// SPDX-License-Identifier: AGPL-3.0-only

#include "vectors.h"

#include <libdeflate.h>
#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::conformance {
namespace {

/// The condensed layout this reader understands. The fetch script stamps
/// the number it wrote into every file, and it is half of the cache
/// directory's name — so a mismatch here means a cache from a different
/// condenser was pointed at, not that a file is corrupt.
constexpr std::uint64_t condenser_version = 1;

/// The cache directory's name, kept identical to cache_key() in
/// scripts/fetch-conformance-vectors.py. It carries the suite commit and
/// the condenser version, so changing either is a new cache rather than a
/// stale one quietly reused.
constexpr std::string_view cache_key = "v2-aea84484abc7-c1";

[[nodiscard]] std::string env(const char* name) {
  // std::getenv, not a platform-specific "safe" variant: this is test
  // apparatus reading its own configuration before anything else runs.
  // MSVC's C4996 for it is turned off for this target in
  // tests/CMakeLists.txt.
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string{value};
}

/// Kept identical to default_cache_dir() in the fetch script.
[[nodiscard]] std::filesystem::path user_cache_root() {
#ifdef _WIN32
  const std::string local = env("LOCALAPPDATA");
  if (!local.empty()) {
    return std::filesystem::path{local};
  }
  return std::filesystem::path{env("USERPROFILE")} / "AppData" / "Local";
#else
  const std::string xdg = env("XDG_CACHE_HOME");
  if (!xdg.empty()) {
    return std::filesystem::path{xdg};
  }
  return std::filesystem::path{env("HOME")} / ".cache";
#endif
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path.string() +
                             " — run scripts/fetch-conformance-vectors.py");
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) {
    throw std::runtime_error("cannot size " + path.string());
  }
  in.seekg(0, std::ios::beg);
  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (!bytes.empty()) {
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in) {
      throw std::runtime_error("short read of " + path.string());
    }
  }
  return bytes;
}

struct deflate_deleter {
  void operator()(libdeflate_decompressor* d) const noexcept {
    libdeflate_free_decompressor(d);
  }
};

[[nodiscard]] std::uint8_t byte_at(const std::string& s, std::size_t i) {
  return static_cast<std::uint8_t>(s[i]);
}

/// Inflate a gzip member straight into a simdjson buffer.
///
/// The gzip trailer's ISIZE gives the exact output size for anything
/// under 4 GiB, which every vector file is by two orders of magnitude, so
/// this normally allocates once and gets it right. It still copes with
/// the disagreement, because a truncated cache file is a thing that
/// happens and "guessed the size wrong" is not a useful diagnosis of it.
[[nodiscard]] simdjson::padded_string inflate(
    const std::string& raw, const std::filesystem::path& path) {
  if (raw.size() < 18 || byte_at(raw, 0) != 0x1F || byte_at(raw, 1) != 0x8B) {
    throw std::runtime_error(path.string() + " is not a gzip file");
  }

  const std::size_t tail = raw.size() - 4;
  const auto expected = static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(byte_at(raw, tail)) |
      (static_cast<std::uint32_t>(byte_at(raw, tail + 1)) << 8u) |
      (static_cast<std::uint32_t>(byte_at(raw, tail + 2)) << 16u) |
      (static_cast<std::uint32_t>(byte_at(raw, tail + 3)) << 24u));

  const std::unique_ptr<libdeflate_decompressor, deflate_deleter> inflater(
      libdeflate_alloc_decompressor());
  if (!inflater) {
    throw std::runtime_error("out of memory allocating a decompressor");
  }

  std::size_t room = expected == 0 ? std::size_t{1} << 20U : expected;
  for (int attempt = 0; attempt < 8; ++attempt) {
    simdjson::padded_string buffer(room);
    std::size_t produced = 0;
    const libdeflate_result result = libdeflate_gzip_decompress(
        inflater.get(), raw.data(), raw.size(), buffer.data(), room, &produced);
    if (result == LIBDEFLATE_SUCCESS) {
      return produced == room
                 ? std::move(buffer)
                 : simdjson::padded_string(buffer.data(), produced);
    }
    if (result != LIBDEFLATE_INSUFFICIENT_SPACE) {
      throw std::runtime_error(path.string() +
                               " is corrupt (gzip decompression failed)");
    }
    room *= 2;
  }
  throw std::runtime_error(path.string() +
                           " did not decompress to a sane size");
}

/// Where each of the suite's register names lives in `cpu::registers`.
/// The word registers are in ModRM order there and in a different order
/// in the vectors, which is exactly why this is a table and not a cast.
void assign_register(cpu::registers& regs, std::string_view name,
                     std::uint16_t value) {
  static constexpr std::array<std::pair<std::string_view, cpu::reg16>, 8>
      words = {{{"ax", cpu::reg16::ax},
                {"cx", cpu::reg16::cx},
                {"dx", cpu::reg16::dx},
                {"bx", cpu::reg16::bx},
                {"sp", cpu::reg16::sp},
                {"bp", cpu::reg16::bp},
                {"si", cpu::reg16::si},
                {"di", cpu::reg16::di}}};
  static constexpr std::array<std::pair<std::string_view, cpu::sreg>, 4>
      segments = {{{"es", cpu::sreg::es},
                   {"cs", cpu::sreg::cs},
                   {"ss", cpu::sreg::ss},
                   {"ds", cpu::sreg::ds}}};

  for (const auto& [key, reg] : words) {
    if (key == name) {
      regs[reg] = value;
      return;
    }
  }
  for (const auto& [key, reg] : segments) {
    if (key == name) {
      regs[reg] = value;
      return;
    }
  }
  if (name == "ip") {
    regs.ip = value;
    return;
  }
  if (name == "flags") {
    // Through load_flags, so the hardwired bits read back as the part
    // reads them rather than as the file happened to say. Silicon and
    // this header already agree; this is what keeps that from being an
    // assumption nobody checks.
    regs.load_flags(value);
    return;
  }
  throw std::runtime_error("unknown register name in the vectors: " +
                           std::string{name});
}

void read_registers(simdjson::ondemand::object fields, cpu::registers& regs) {
  for (auto field : fields) {
    const std::string_view name = field.unescaped_key();
    const std::uint64_t value = field.value().get_uint64();
    assign_register(regs, name, static_cast<std::uint16_t>(value));
  }
}

void read_memory(simdjson::ondemand::array entries,
                 std::vector<memory_byte>& out) {
  for (auto entry : entries) {
    std::array<std::uint64_t, 2> pair{};
    std::size_t index = 0;
    for (auto number : entry.get_array()) {
      if (index < pair.size()) {
        pair[index] = number.get_uint64();
      }
      ++index;
    }
    if (index != 2) {
      throw std::runtime_error("a ram entry is not an [address, value] pair");
    }
    out.push_back({.address = static_cast<std::uint32_t>(pair[0]),
                   .value = static_cast<std::uint8_t>(pair[1])});
  }
}

void read_ports(simdjson::ondemand::array entries, std::vector<port_op>& out) {
  for (auto entry : entries) {
    port_op op{};
    std::size_t index = 0;
    for (auto element : entry.get_array()) {
      if (index == 0) {
        const std::uint64_t port = element.get_uint64();
        op.port = static_cast<std::uint16_t>(port);
      } else if (index == 1) {
        const std::uint64_t value = element.get_uint64();
        op.value = static_cast<std::uint8_t>(value);
      } else {
        const std::string_view direction = element.get_string();
        op.what = direction == "r" ? port_op::kind::read : port_op::kind::write;
      }
      ++index;
    }
    if (index != 3) {
      throw std::runtime_error("an io entry is not a [port, value, r/w] tuple");
    }
    out.push_back(op);
  }
}

/// One test object. The condenser writes the fields in a fixed order and
/// stamps the version that says so, which is what lets "final" fold onto
/// the state "initial" just built.
void read_test(simdjson::ondemand::object fields, vector_test& out) {
  for (auto field : fields) {
    const std::string_view key = field.unescaped_key();
    if (key == "name") {
      const std::string_view name = field.value().get_string();
      out.name.assign(name);
    } else if (key == "idx") {
      const std::uint64_t idx = field.value().get_uint64();
      out.idx = static_cast<std::uint32_t>(idx);
    } else if (key == "bytes") {
      for (auto byte : field.value().get_array()) {
        const std::uint64_t value = byte.get_uint64();
        out.bytes.push_back(static_cast<std::uint8_t>(value));
      }
    } else if (key == "initial") {
      for (auto part : field.value().get_object()) {
        const std::string_view what = part.unescaped_key();
        if (what == "regs") {
          read_registers(part.value().get_object(), out.before);
        } else {
          read_memory(part.value().get_array(), out.ram_before);
        }
      }
      out.after = out.before;
    } else if (key == "final") {
      for (auto part : field.value().get_object()) {
        const std::string_view what = part.unescaped_key();
        if (what == "regs") {
          read_registers(part.value().get_object(), out.after);
        } else {
          read_memory(part.value().get_array(), out.ram_after);
        }
      }
    } else if (key == "io") {
      read_ports(field.value().get_array(), out.ports);
    }
  }
}

}  // namespace

std::filesystem::path vector_cache_dir() {
  const std::string chosen = env("AMBERFOLIO_CONFORMANCE_VECTORS");
  if (!chosen.empty()) {
    return std::filesystem::path{chosen};
  }
  return user_cache_root() / "amberfolio" / "conformance" /
         std::string{cache_key};
}

std::filesystem::path vector_path(std::string_view stem) {
  return vector_cache_dir() / (std::string{stem} + ".json.gz");
}

std::size_t test_limit() {
  const std::string value = env("AMBERFOLIO_CONFORMANCE_LIMIT");
  if (value.empty()) {
    return 0;
  }
  try {
    const long long limit = std::stoll(value);
    return limit <= 0 ? 0 : static_cast<std::size_t>(limit);
  } catch (const std::exception&) {
    throw std::runtime_error("AMBERFOLIO_CONFORMANCE_LIMIT is not a number: " +
                             value);
  }
}

vector_file load_vectors(std::string_view stem, std::size_t limit) {
  const std::filesystem::path path = vector_path(stem);
  const simdjson::padded_string text = inflate(read_file(path), path);

  simdjson::ondemand::parser parser;
  simdjson::ondemand::document document = parser.iterate(text);

  vector_file file;
  file.stem = std::string{stem};

  // The header fields come first and "tests" comes last, so this walks
  // the object once, in order, and returns from inside the array rather
  // than trying to resume the outer iteration after stopping early.
  for (auto field : document.get_object()) {
    const std::string_view key = field.unescaped_key();
    if (key == "condenser") {
      const std::uint64_t version = field.value().get_uint64();
      if (version != condenser_version) {
        throw std::runtime_error(
            path.string() + " was written by condenser version " +
            std::to_string(version) + ", but this build reads version " +
            std::to_string(condenser_version) +
            " — re-run scripts/fetch-conformance-vectors.py");
      }
    } else if (key == "count") {
      const std::uint64_t count = field.value().get_uint64();
      const std::uint64_t wanted =
          limit == 0 ? count : std::min<std::uint64_t>(count, limit);
      file.tests.reserve(static_cast<std::size_t>(wanted));
    } else if (key == "tests") {
      for (auto test : field.value().get_array()) {
        if (limit != 0 && file.tests.size() == limit) {
          break;
        }
        read_test(test.get_object(), file.tests.emplace_back());
      }
      if (file.tests.empty()) {
        throw std::runtime_error(path.string() + " holds no tests");
      }
      return file;
    }
  }

  throw std::runtime_error(path.string() + " has no tests array");
}

}  // namespace amberfolio::conformance
