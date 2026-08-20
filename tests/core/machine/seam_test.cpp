// SPDX-License-Identifier: AGPL-3.0-only
//
// The seam engine (seam.h, PLAN.md §5): what it refuses, what it costs
// when nothing is on, and — the one that matters — that an armed
// interception point runs its handler at the step boundary *before* the
// instruction there is fetched.
//
// The code-wheel seam is exercised through its mechanism and not through
// any program: the test writes the two instructions the handler cares
// about itself, from the encoding, and points the machine at them. Every
// byte here is this file's own (PLAN.md §6).

#include "amberfolio/machine/seam.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// The digest a seam definition names, as bytes. Derived from the
/// definition itself rather than pasted: a test that hard-coded the
/// fingerprint would pass even if `enable()` compared the wrong thing.
[[nodiscard]] sha256_digest digest_of(std::string_view hex) {
  sha256_digest digest;
  const auto nibble = [](char c) -> std::uint8_t {
    if (c >= '0' && c <= '9') {
      return static_cast<std::uint8_t>(c - '0');
    }
    return static_cast<std::uint8_t>(c - 'a' + 10);
  };
  for (std::size_t i = 0; i < sha256_digest::byte_length; ++i) {
    digest.bytes[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4U) |
                                                nibble(hex[i * 2 + 1]));
  }
  return digest;
}

[[nodiscard]] const seam_definition& code_wheel() {
  for (const seam_definition& seam : all_seams()) {
    if (seam.id == "code-wheel") {
      return seam;
    }
  }
  ADD_FAILURE() << "the code-wheel seam is not in the table";
  return all_seams().front();
}

/// A machine with the code-wheel seam's own binary claimed to be loaded
/// at the segment DOS would have put it at.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(digest_of(code_wheel().fingerprint),
                        image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// Put `bytes` at the seam's own interception offset and point the
  /// processor at it, with a stack.
  void program_at(std::uint32_t image_offset,
                  std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t at =
        cpu::physical_address(image_load_segment, 0) + image_offset;
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[at] = byte;
      ++at;
    }
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r[cpu::sreg::ss] = image_load_segment;
    r[cpu::reg16::sp] = 0x0400;
    r.ip = static_cast<std::uint16_t>(image_offset);
  }

  std::unique_ptr<machine> box;
};

/// The offset of the seam's one interception point, and the offset of the
/// word table its handler qualifies on. Read out of the definition rather
/// than restated, so this file cannot drift from it.
[[nodiscard]] std::uint32_t interception_offset() {
  return code_wheel().points.front().image_offset;
}

/// Inside the candidate-word table, comfortably: the first entry's
/// characters. The table's own geometry is seam_code_wheel.cpp's; all a
/// test needs is an address the handler must accept and one it must not.
constexpr std::uint16_t in_the_table = 0xC7C3;
constexpr std::uint16_t not_in_the_table = 0x0100;

// --- What it refuses -----------------------------------------------------

TEST(seam_enable, refuses_before_a_program_is_known) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  EXPECT_EQ(box->seams().enable("code-wheel"), seam_error::no_program);
  EXPECT_FALSE(box->seams().armed());
}

TEST(seam_enable, refuses_a_name_that_is_not_a_seam) {
  const rig r;
  EXPECT_EQ(r.pc().seams().enable("no-such-seam"), seam_error::unknown_seam);
  EXPECT_FALSE(r.pc().seams().armed());
}

TEST(seam_enable, refuses_a_binary_the_addresses_are_not_about) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other = digest_of(code_wheel().fingerprint);
  other.bytes[0] = static_cast<std::uint8_t>(other.bytes[0] ^ 0xFFU);
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().enable("code-wheel"), seam_error::wrong_binary);
  EXPECT_FALSE(box->seams().armed());
}

// --- What it does when it is on ------------------------------------------

TEST(seam_enable, arms_and_says_what_is_on) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_error::none);
  EXPECT_TRUE(r.pc().seams().armed());
  ASSERT_EQ(r.pc().seams().enabled().size(), 1u);
  EXPECT_EQ(r.pc().seams().enabled()[0], "code-wheel");
}

TEST(seam_enable, a_reset_machine_has_no_program_and_no_seams) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_error::none);
  ASSERT_TRUE(r.pc().seams().armed());

  r.pc().reset();

  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_TRUE(r.pc().seams().enabled().empty());
  EXPECT_EQ(r.pc().seams().enable("code-wheel"), seam_error::no_program);
}

TEST(seam_engine_cost, an_unarmed_machine_runs_the_program_untouched) {
  const rig r;
  // No enable() at all: the instruction runs exactly as written.
  r.program_at(interception_offset(), {0xF3, 0xA6, 0xF4});  // repe cmpsb; hlt
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 4;
  regs.set(cpu::reg8::al, 1);
  regs.set(cpu::reg8::ah, 6);
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;

  r.pc().step();

  EXPECT_NE(regs[cpu::reg16::cx], 4u) << "the compare should have run";
  EXPECT_EQ(regs.get(cpu::reg8::al), 1) << "and nothing should have edited AL";
}

// --- The code-wheel handler ---------------------------------------------
//
// Its whole contract: at the compare loop, when the expected operand
// points into the candidate-word table, leave the program's own routine
// nothing to disagree about — no iterations, the zero flag set, and the
// two lengths equal.

TEST(seam_code_wheel, makes_the_programs_own_compare_report_equal) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_error::none);

  r.program_at(interception_offset(), {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);  // one character typed
  regs.set(cpu::reg8::ah, 6);  // six expected
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;
  regs.set_flag(cpu::flag::zf, false);

  r.pc().step();

  EXPECT_EQ(regs[cpu::reg16::cx], 0u) << "no iteration to disagree over";
  EXPECT_TRUE(regs.flag_set(cpu::flag::zf));
  EXPECT_EQ(regs.get(cpu::reg8::al), regs.get(cpu::reg8::ah))
      << "and the length comparison after it agrees too";
}

TEST(seam_code_wheel, leaves_every_other_string_comparison_alone) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_error::none);

  // The same routine, the same instruction — but comparing against
  // something that is not a candidate word, which is what the program
  // does roughly a hundred and fifty times before the gate.
  r.program_at(interception_offset(), {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);
  regs.set(cpu::reg8::ah, 6);
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = not_in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;

  r.pc().step();

  EXPECT_NE(regs[cpu::reg16::cx], 0u) << "the compare should have run";
  EXPECT_EQ(regs.get(cpu::reg8::al), 1) << "and AL should be untouched";
}

TEST(seam_code_wheel, does_nothing_anywhere_but_its_own_address) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_error::none);

  // The identical instruction, one paragraph earlier. A seam is a point,
  // not a pattern.
  r.program_at(interception_offset() - 0x10, {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);
  regs.set(cpu::reg8::ah, 6);
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;

  r.pc().step();

  EXPECT_NE(regs[cpu::reg16::cx], 0u);
  EXPECT_EQ(regs.get(cpu::reg8::al), 1);
}

}  // namespace
}  // namespace amberfolio::machine
