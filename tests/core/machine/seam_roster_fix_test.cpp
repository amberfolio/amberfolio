// SPDX-License-Identifier: AGPL-3.0-only
//
// The roster fix (seam_roster_fix.cpp), a built-in fix: on whenever the
// program it names is loaded, and listed nowhere. Exercised through its
// mechanism and not through any program: the test stands the processor on the list
// routine's exit with a list of nodes laid out on a heap, the caller's head
// pointer where the frame says it is, a save directory in the data segment
// and the files the directory holds in a filesystem, and reads the list and
// a log a FreeMem stand-in keeps after the batches the handler queued have
// run themselves out.
//
// The offsets below are restated rather than read out of the seam, which is
// the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The names, the directory and the
// characters a name loses are made up, and **every byte here is this file's
// own** (PLAN.md §6).

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "roster-fix";

/// The list routine's exit, in overlay 17, and the word the program's
/// overlay manager keeps that overlay's segment in.
constexpr std::uint16_t point = 0x0191;
constexpr std::uint32_t word_list = 0x7D0;
constexpr std::uint16_t list_segment = 0x7800;

/// The program's data segment is the image's paragraph 0xC7C on.
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xC7C);
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t frame_head = 6;
/// Where the caller keeps its head pointer.
constexpr std::uint16_t head_variable = 0x0700;

/// The save directory and the ten characters a name loses.
constexpr std::uint16_t data_save_path = 0x537A;
constexpr std::uint16_t data_strip = 0x0D1A;
constexpr std::string_view strip = " .,-'!?;:/";

/// The nodes: forty-six bytes, the name at `+0`, the next at `+0x2A`.
constexpr std::uint16_t heap_segment = 0x3000;
constexpr std::uint16_t node_bytes = 0x2E;
constexpr std::uint16_t node_next = 0x2A;

/// The program's FreeMem, from the image segment, and the stand-in's log:
/// a count and then the size, offset and segment of each call.
constexpr std::uint16_t free_paragraph = 0x0AF8;
constexpr std::uint16_t free_offset = 0x0254;
constexpr std::uint16_t log_count = 0x7000;
constexpr std::uint16_t log_base = 0x7002;

struct freed {
  std::uint16_t segment;
  std::uint16_t offset;
  std::uint16_t size;

  bool operator==(const freed&) const = default;
};

[[nodiscard]] dos_path path_named(std::string_view text) {
  const vfs_result<dos_path> where =
      canonicalize(dos_path{}, std::span<const char>(text.data(), text.size()));
  EXPECT_TRUE(where.ok()) << text;
  return where.value;
}

struct rig {
  explicit rig(bool with_files = true)
      : fs(std::make_unique<memory_filesystem>()),
        box(std::make_unique<machine>(memory_layout::pc)) {
    if (with_files) {
      box->set_filesystem(*fs);
    }
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    put_string(data_segment, data_strip, strip, false);
    save_path("C:\\SAVE\\");
    EXPECT_EQ(fs->mkdir(path_named("\\SAVE")), vfs_error::none);
    free_memory();
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_head),
             head_variable);
    put_word(stack_segment,
             static_cast<std::uint16_t>(frame_base + frame_head + 2),
             stack_segment);
  }

  /// The overlay is in: the program's manager says where. The fix itself
  /// came on with the program.
  void arm() const {
    put_word(image_load_segment, static_cast<std::uint16_t>(word_list),
             list_segment);
  }

  [[nodiscard]] const seam_definition& fix() const {
    for (const seam_definition& s : built_in_fixes()) {
      if (s.id == seam_id) {
        return s;
      }
    }
    ADD_FAILURE() << "no built-in fix named " << seam_id;
    return built_in_fixes().front();
  }

  void put_byte(std::uint16_t segment, std::uint16_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = value;
  }

  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(value));
    put_byte(segment, static_cast<std::uint16_t>(offset + 1),
             static_cast<std::uint8_t>(value >> 8U));
  }

  [[nodiscard]] std::uint8_t byte(std::uint16_t segment,
                                  std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    return static_cast<std::uint16_t>(byte(segment, offset) |
                                      (byte(segment, offset + 1U) << 8U));
  }

  /// `text` at `segment:offset`, as a Pascal string or as bare bytes.
  void put_string(std::uint16_t segment, std::uint16_t offset,
                  std::string_view text, bool counted = true) const {
    std::uint16_t at = offset;
    if (counted) {
      put_byte(segment, at++, static_cast<std::uint8_t>(text.size()));
    }
    for (const char c : text) {
      put_byte(segment, at++, static_cast<std::uint8_t>(c));
    }
  }

  void save_path(std::string_view text) const {
    put_string(data_segment, data_save_path, text);
  }

  /// A file in the machine's filesystem.
  void holds(std::string_view path) const {
    const vfs_result<file_handle> made = fs->create(path_named(path));
    ASSERT_TRUE(made.ok()) << path;
    EXPECT_EQ(fs->close(made.value), vfs_error::none);
  }

  [[nodiscard]] static std::uint16_t node_at(std::size_t nth) {
    return static_cast<std::uint16_t>(0x0100 + (nth * 0x40));
  }

  /// The list the routine built: one node per name, in order, each on the
  /// heap at `node_at(n)`, and the caller's head pointing at the first.
  void list(std::initializer_list<std::string_view> names) const {
    std::size_t nth = 0;
    for (const std::string_view name : names) {
      const std::uint16_t node = node_at(nth);
      for (std::uint16_t i = 0; i < node_bytes; ++i) {
        put_byte(heap_segment, static_cast<std::uint16_t>(node + i), 0);
      }
      put_string(heap_segment, node, name);
      const bool last = nth + 1 == names.size();
      put_word(heap_segment, static_cast<std::uint16_t>(node + node_next),
               last ? 0 : node_at(nth + 1));
      put_word(heap_segment, static_cast<std::uint16_t>(node + node_next + 2),
               last ? 0 : heap_segment);
      ++nth;
    }
    put_word(stack_segment, head_variable, names.size() == 0 ? 0 : node_at(0));
    put_word(stack_segment, static_cast<std::uint16_t>(head_variable + 2),
             names.size() == 0 ? 0 : heap_segment);
  }

  /// The names the list holds now, walked from the caller's head.
  [[nodiscard]] std::vector<std::string> names() const {
    std::vector<std::string> out;
    std::uint16_t offset = word(stack_segment, head_variable);
    std::uint16_t segment =
        word(stack_segment, static_cast<std::uint16_t>(head_variable + 2));
    while ((segment != 0 || offset != 0) && out.size() < 64) {
      std::string name;
      const std::uint8_t length = byte(segment, offset);
      for (std::uint8_t i = 0; i < length; ++i) {
        name.push_back(static_cast<char>(
            byte(segment, static_cast<std::uint16_t>(offset + 1U + i))));
      }
      out.push_back(name);
      const std::uint16_t next_offset =
          word(segment, static_cast<std::uint16_t>(offset + node_next));
      segment =
          word(segment, static_cast<std::uint16_t>(offset + node_next + 2));
      offset = next_offset;
    }
    return out;
  }

  /// The FreeMem stand-in: counts the call and logs its three words.
  ///
  ///     push bp / mov bp, sp / mov di, [log_count] / shl di, 1 / ...
  ///
  /// spelled out below, then `retf 6`, which is what the program's own
  /// cleans.
  void free_memory() const {
    const auto segment =
        static_cast<std::uint16_t>(image_load_segment + free_paragraph);
    std::uint16_t put = free_offset;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
      for (const std::uint8_t b : bytes) {
        put_byte(segment, put++, b);
      }
    };
    const auto lo = [](std::uint16_t v) {
      return static_cast<std::uint8_t>(v & 0xFFU);
    };
    const auto hi = [](std::uint16_t v) {
      return static_cast<std::uint8_t>(v >> 8U);
    };
    emit({0x55, 0x89, 0xE5});                          // push bp; mov bp,sp
    emit({0x8B, 0x3E, lo(log_count), hi(log_count)});  // mov di,[count]
    emit({0x89, 0xF8, 0xD1, 0xE7, 0x01, 0xC7});        // di = count * 3
    emit({0xD1, 0xE7});                                // di *= 2
    emit({0x81, 0xC7, lo(log_base), hi(log_base)});    // di += log_base
    for (const std::uint8_t k :
         std::array<std::uint8_t, 3>{0x0A, 0x08, 0x06}) {  // seg, off, size
      emit({0x8B, 0x46, k, 0x89, 0x05, 0x83, 0xC7, 0x02});
    }
    emit({0xFF, 0x06, lo(log_count), hi(log_count)});  // inc word [count]
    emit({0x5D, 0xCA, 0x06, 0x00});                    // pop bp; retf 6
  }

  [[nodiscard]] std::vector<freed> frees() const {
    std::vector<freed> out;
    const std::uint16_t count = word(data_segment, log_count);
    for (std::uint16_t n = 0; n < count && n < 16; ++n) {
      const auto at = static_cast<std::uint16_t>(log_base + (n * 6));
      out.push_back(
          {.segment = word(data_segment, at),
           .offset = word(data_segment, static_cast<std::uint16_t>(at + 2)),
           .size = word(data_segment, static_cast<std::uint16_t>(at + 4))});
    }
    return out;
  }

  /// Stand on the exit, take a step, and let every batch the arrivals
  /// queue run itself out. The instruction there is a HLT.
  void arrive() const {
    put_byte(list_segment, point, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = list_segment;
    r.ip = point;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    for (unsigned nth = 0; nth < 8192 && !box->processor().halted(); ++nth) {
      box->step();
    }
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().fix_status(seam_id);
  }

  std::unique_ptr<memory_filesystem> fs;
  std::unique_ptr<machine> box;
};

// --- The definition --------------------------------------------------------

TEST(SeamRosterFix, IsOnePointAtTheListRoutinesExitInOverlay17) {
  const rig r;
  const seam_definition* s = &r.fix();
  EXPECT_FALSE(s->about.empty());
  EXPECT_FALSE(s->trigger) << "a setting: nothing to pull";
  EXPECT_EQ(s->gate, document_kind::none);
  EXPECT_TRUE(s->group.empty());
  EXPECT_EQ(s->schema, seam_schema_version);
  ASSERT_EQ(s->points.size(), 1u);

  const seam_point& p = s->points[0];
  EXPECT_EQ(p.offset, point);
  EXPECT_FALSE(p.module.is_resident_image());
  EXPECT_EQ(p.module.file, "GAME.OVR");
  EXPECT_EQ(p.module.file_offset, 122854u);
  EXPECT_EQ(p.module.length, 9189u);
  EXPECT_EQ(p.module.load_segment_at, word_list);
  EXPECT_FALSE(p.module.digest.empty());
  EXPECT_FALSE(p.at_every_step);
  EXPECT_FALSE(p.inside_calls);
}

TEST(SeamRosterFix, IsOnAsSoonAsTheProgramIsLoaded) {
  const rig r;
  EXPECT_EQ(r.status().state, seam_state::on);
  EXPECT_EQ(r.box->seams().enabled_count(), 0u)
      << "a fix is not a seam anybody turned on";
}

TEST(SeamRosterFix, IsListedNowhereAndCannotBeToggled) {
  const rig r;
  for (const seam_definition& s : all_seams()) {
    EXPECT_NE(s.id, seam_id);
  }
  for (std::size_t i = 0; i < r.box->seams().count(); ++i) {
    EXPECT_NE(r.box->seams().status(i).id, seam_id);
  }
  EXPECT_EQ(r.box->seams().find(seam_id), nullptr);
  EXPECT_TRUE(r.box->seams().status(seam_id).id.empty());
  EXPECT_EQ(r.box->seams().enable(seam_id), seam_reason::unknown_seam);
  EXPECT_EQ(r.box->seams().disable(seam_id), seam_reason::unknown_seam);
  EXPECT_EQ(r.status().state, seam_state::on) << "and is still on";
}

TEST(SeamRosterFix, IsOffBeforeAProgramAndOnAgainAfterTheNext) {
  const rig r;
  r.box->seams().clear();
  EXPECT_NE(r.status().state, seam_state::on);
  sha256_digest baseline;
  ASSERT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
  r.box->seams().loaded(baseline, image_load_segment);
  EXPECT_EQ(r.status().state, seam_state::on);
}

TEST(SeamRosterFix, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(box->seams().fix_status(seam_id).state, seam_state::unavailable);
  EXPECT_FALSE(box->seams().armed());
}

TEST(SeamRosterFix, IsInertWhileOverlay17IsNotLoaded) {
  const rig r;
  r.arm();
  r.put_word(image_load_segment, static_cast<std::uint16_t>(word_list), 0);
  r.list({"OSSIAN"});

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"OSSIAN"}));
  EXPECT_EQ(r.status().fired, 0u);
}

// --- Over a roster whose files are all there -------------------------------

TEST(SeamRosterFix, LeavesAListWhoseFilesAreAllThereAsItIs) {
  const rig r;
  r.arm();
  r.list({"WREN", "OSSIAN"});
  r.holds("\\SAVE\\WREN.CHA");
  r.holds("\\SAVE\\OSSIAN.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN", "OSSIAN"}));
  EXPECT_TRUE(r.frees().empty());
  EXPECT_EQ(r.status().fired, 1u);
  EXPECT_EQ(r.status().declined, 0u);
}

TEST(SeamRosterFix, LeavesAnEmptyListEmpty) {
  const rig r;
  r.arm();
  r.list({});

  r.arrive();
  EXPECT_TRUE(r.names().empty());
  EXPECT_TRUE(r.frees().empty());
}

TEST(SeamRosterFix, CutsANameToItsStemTheWayTheProgramDoes) {
  const rig r;
  r.arm();
  // The made-up characters go, the rest is cut to eight, and the case
  // does not matter.
  r.list({"Tamsin O'Reilly-Vane", "A.B. C"});
  r.holds("\\SAVE\\TAMSINOR.CHA");
  r.holds("\\SAVE\\ABC.CHA");

  r.arrive();
  EXPECT_EQ(r.names(),
            (std::vector<std::string>{"Tamsin O'Reilly-Vane", "A.B. C"}));
  EXPECT_TRUE(r.frees().empty());
}

TEST(SeamRosterFix, ResolvesARelativeSaveDirectoryInTheCurrentOne) {
  const rig r;
  r.arm();
  ASSERT_EQ(r.fs->mkdir(path_named("\\GAME")), vfs_error::none);
  ASSERT_EQ(r.fs->mkdir(path_named("\\GAME\\KEEP")), vfs_error::none);
  ASSERT_EQ(r.box->dos().set_current_directory(*r.fs, path_named("\\GAME")),
            vfs_error::none);
  r.save_path("KEEP\\");
  r.list({"WREN", "OSSIAN"});
  r.holds(R"(\GAME\KEEP\WREN.CHA)");
  r.holds("\\SAVE\\OSSIAN.CHA");  // in a directory the program does not ask

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN"}));
}

// --- A name whose file is gone ---------------------------------------------

TEST(SeamRosterFix, LeavesOutANameWhoseFileIsGone) {
  const rig r;
  r.arm();
  r.list({"WREN", "OSSIAN", "BRIAR"});
  r.holds("\\SAVE\\WREN.CHA");
  r.holds("\\SAVE\\BRIAR.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN", "BRIAR"}));
  EXPECT_EQ(r.frees(),
            (std::vector<freed>{{heap_segment, rig::node_at(1), node_bytes}}));
}

TEST(SeamRosterFix, MovesTheHeadWhenTheFirstNameIsTheOneGone) {
  const rig r;
  r.arm();
  r.list({"OSSIAN", "WREN"});
  r.holds("\\SAVE\\WREN.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN"}));
  EXPECT_EQ(r.word(stack_segment, head_variable), rig::node_at(1));
  EXPECT_EQ(r.frees(),
            (std::vector<freed>{{heap_segment, rig::node_at(0), node_bytes}}));
}

TEST(SeamRosterFix, LeavesOutEveryOneOfThemOnePerArrival) {
  const rig r;
  r.arm();
  r.list({"OSSIAN", "WREN", "BRIAR", "IVO"});
  r.holds("\\SAVE\\WREN.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN"}));
  EXPECT_EQ(r.frees(),
            (std::vector<freed>{{heap_segment, rig::node_at(0), node_bytes},
                                {heap_segment, rig::node_at(2), node_bytes},
                                {heap_segment, rig::node_at(3), node_bytes}}));
}

TEST(SeamRosterFix, LeavesAListWithNobodyInItWhenNoFileIsThere) {
  const rig r;
  r.arm();
  r.list({"OSSIAN", "BRIAR"});

  r.arrive();
  EXPECT_TRUE(r.names().empty());
  EXPECT_EQ(r.word(stack_segment, head_variable), 0u);
  EXPECT_EQ(r.word(stack_segment, head_variable + 2U), 0u);
  EXPECT_EQ(r.frees().size(), 2u);
}

TEST(SeamRosterFix, KeepsANameMarkedAsInTheParty) {
  const rig r;
  r.arm();
  // The load that put it in the party unlinked its file; the program
  // handles the mark.
  r.list({"* OSSIAN", "WREN"});
  r.holds("\\SAVE\\WREN.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"* OSSIAN", "WREN"}));
  EXPECT_TRUE(r.frees().empty());
}

TEST(SeamRosterFix, LeavesOutANameNoStemCanSpell) {
  const rig r;
  r.arm();
  r.list({"WREN", "?!"});
  r.holds("\\SAVE\\WREN.CHA");

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"WREN"}));
}

// --- What it refuses -------------------------------------------------------

TEST(SeamRosterFix, DeclinesWhenTheSaveDirectoryIsNotThere) {
  const rig r;
  r.arm();
  r.save_path("C:\\ELSEWHERE\\");
  r.list({"OSSIAN"});

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"OSSIAN"}));
  EXPECT_TRUE(r.frees().empty());
  EXPECT_EQ(r.status().declined, 1u);
}

TEST(SeamRosterFix, DeclinesASaveDirectoryLongerThanAnyTheProgramReads) {
  const rig r;
  r.arm();
  r.save_path(std::string(0x60, 'A'));
  r.list({"OSSIAN"});

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"OSSIAN"}));
  EXPECT_EQ(r.status().declined, 1u);
}

TEST(SeamRosterFix, DeclinesWithNoFilesystem) {
  const rig r(false);
  r.arm();
  r.list({"OSSIAN"});

  r.arrive();
  EXPECT_EQ(r.names(), (std::vector<std::string>{"OSSIAN"}));
  EXPECT_EQ(r.status().declined, 1u);
}

TEST(SeamRosterFix, DeclinesANodeLongerThanANodeHolds) {
  const rig r;
  r.arm();
  // The first name's file is gone too, and it stays: nothing in a list
  // the walk cannot finish is touched.
  r.list({"WREN", "OSSIAN"});
  r.put_byte(heap_segment, rig::node_at(1), 0x30);

  r.arrive();
  EXPECT_TRUE(r.frees().empty());
  EXPECT_EQ(r.status().declined, 1u);
  EXPECT_EQ(r.word(stack_segment, head_variable), rig::node_at(0));
}

TEST(SeamRosterFix, DeclinesANodeOutsideConventionalMemory) {
  const rig r;
  r.arm();
  r.list({"WREN"});
  r.holds("\\SAVE\\WREN.CHA");
  r.put_word(heap_segment,
             static_cast<std::uint16_t>(rig::node_at(0) + node_next), 0);
  r.put_word(heap_segment,
             static_cast<std::uint16_t>(rig::node_at(0) + node_next + 2),
             0xA000);

  r.arrive();
  EXPECT_TRUE(r.frees().empty());
  EXPECT_EQ(r.status().declined, 1u);
}

TEST(SeamRosterFix, DeclinesAListThatGoesRoundInACircle) {
  const rig r;
  r.arm();
  r.list({"WREN", "IVO"});
  r.holds("\\SAVE\\WREN.CHA");
  r.holds("\\SAVE\\IVO.CHA");
  r.put_word(heap_segment,
             static_cast<std::uint16_t>(rig::node_at(1) + node_next),
             rig::node_at(0));
  r.put_word(heap_segment,
             static_cast<std::uint16_t>(rig::node_at(1) + node_next + 2),
             heap_segment);

  r.arrive();
  EXPECT_TRUE(r.frees().empty());
  EXPECT_EQ(r.status().declined, 1u);
}

}  // namespace
}  // namespace amberfolio::machine
